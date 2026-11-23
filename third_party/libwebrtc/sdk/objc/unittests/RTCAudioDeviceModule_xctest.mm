/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <XCTest/XCTest.h>

#include <stdlib.h>

#if defined(WEBRTC_IOS)
#import "sdk/objc/native/api/audio_device_module.h"
#endif

#include <optional>

#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"

typedef int32_t (^NeedMorePlayDataBlock)(const size_t nSamples,
                                         const size_t nBytesPerSample,
                                         const size_t nChannels,
                                         const uint32_t samplesPerSec,
                                         void *audioSamples,
                                         size_t &nSamplesOut,
                                         int64_t *elapsed_time_ms,
                                         int64_t *ntp_time_ms);

typedef int32_t (^RecordedDataIsAvailableBlock)(
    const void *audioSamples,
    const size_t nSamples,
    const size_t nBytesPerSample,
    const size_t nChannels,
    const uint32_t samplesPerSec,
    const uint32_t totalDelayMS,
    const int32_t clockDrift,
    const uint32_t currentMicLevel,
    const bool keyPressed,
    uint32_t &newMicLevel,
    std::optional<int64_t> estimatedCaptureTimeNS);

// This class implements the AudioTransport API and forwards all methods to the
// appropriate blocks.
class MockAudioTransport : public webrtc::AudioTransport {
 public:
  MockAudioTransport() {}
  ~MockAudioTransport() override {}

  void expectNeedMorePlayData(NeedMorePlayDataBlock block) {
    needMorePlayDataBlock = block;
  }

  void expectRecordedDataIsAvailable(RecordedDataIsAvailableBlock block) {
    recordedDataIsAvailableBlock = block;
  }

  int32_t NeedMorePlayData(const size_t nSamples,
                           const size_t nBytesPerSample,
                           const size_t nChannels,
                           const uint32_t samplesPerSec,
                           void *audioSamples,
                           size_t &nSamplesOut,
                           int64_t *elapsed_time_ms,
                           int64_t *ntp_time_ms) override {
    return needMorePlayDataBlock(nSamples,
                                 nBytesPerSample,
                                 nChannels,
                                 samplesPerSec,
                                 audioSamples,
                                 nSamplesOut,
                                 elapsed_time_ms,
                                 ntp_time_ms);
  }

  int32_t RecordedDataIsAvailable(
      const void *audioSamples,
      const size_t nSamples,
      const size_t nBytesPerSample,
      const size_t nChannels,
      const uint32_t samplesPerSec,
      const uint32_t totalDelayMS,
      const int32_t clockDrift,
      const uint32_t currentMicLevel,
      const bool keyPressed,
      uint32_t &newMicLevel,
      std::optional<int64_t> estimatedCaptureTimeNS) override {
    return recordedDataIsAvailableBlock(audioSamples,
                                        nSamples,
                                        nBytesPerSample,
                                        nChannels,
                                        samplesPerSec,
                                        totalDelayMS,
                                        clockDrift,
                                        currentMicLevel,
                                        keyPressed,
                                        newMicLevel,
                                        estimatedCaptureTimeNS);
  }

  void PullRenderData(int bits_per_sample,
                      int sample_rate,
                      size_t number_of_channels,
                      size_t number_of_frames,
                      void *audio_data,
                      int64_t *elapsed_time_ms,
                      int64_t *ntp_time_ms) override {}

 private:
  NeedMorePlayDataBlock needMorePlayDataBlock;
  RecordedDataIsAvailableBlock recordedDataIsAvailableBlock;
};

// Number of callbacks (input or output) the tests waits for before we set
// an event indicating that the test was OK.
static const NSUInteger kNumCallbacks = 10;
// Max amount of time we wait for an event to be set while counting callbacks.
static const NSTimeInterval kTestTimeOutInSec = 20.0;
// Number of bits per PCM audio sample.
static const NSUInteger kBitsPerSample = 16;
// Number of bytes per PCM audio sample.
static const NSUInteger kBytesPerSample = kBitsPerSample / 8;
// Average number of audio callbacks per second assuming 10ms packet size.
static const NSUInteger kNumCallbacksPerSecond = 100;
// Play out a test file during this time (unit is in seconds).
static const NSUInteger kFilePlayTimeInSec = 15;
// Run the full-duplex test during this time (unit is in seconds).
// Note that first `kNumIgnoreFirstCallbacks` are ignored.
static const NSUInteger kFullDuplexTimeInSec = 10;
// Wait for the callback sequence to stabilize by ignoring this amount of the
// initial callbacks (avoids initial FIFO access).
// Only used in the RunPlayoutAndRecordingInFullDuplex test.
static const NSUInteger kNumIgnoreFirstCallbacks = 50;

@interface RTCAudioDeviceModuleTests : XCTestCase {
  bool _testEnabled;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> audioDeviceModule;
  MockAudioTransport mock;
}

@property(nonatomic, assign) webrtc::AudioParameters playoutParameters;
@property(nonatomic, assign) webrtc::AudioParameters recordParameters;

@end

@implementation RTCAudioDeviceModuleTests

@synthesize playoutParameters;
@synthesize recordParameters;

- (void)setUp {
  [super setUp];
#if defined(WEBRTC_IOS) && TARGET_OS_SIMULATOR
  // TODO(peterhanspers): Reenable these tests on simulator.
  // See bugs.webrtc.org/7812
  _testEnabled = false;
  if (::getenv("WEBRTC_IOS_RUN_AUDIO_TESTS") != nullptr) {
    _testEnabled = true;
  }
#else
  _testEnabled = true;
#endif

  audioDeviceModule =
      webrtc::CreateAudioDeviceModule(webrtc::CreateEnvironment());
  XCTAssertEqual(0, audioDeviceModule->Init());
  XCTAssertEqual(
      0, audioDeviceModule->GetPlayoutAudioParameters(&playoutParameters));
  XCTAssertEqual(
      0, audioDeviceModule->GetRecordAudioParameters(&recordParameters));
}

- (void)tearDown {
  XCTAssertEqual(0, audioDeviceModule->Terminate());
  audioDeviceModule = nullptr;
  [super tearDown];
}

- (void)startPlayout {
  XCTAssertFalse(audioDeviceModule->Playing());
  XCTAssertEqual(0, audioDeviceModule->InitPlayout());
  XCTAssertTrue(audioDeviceModule->PlayoutIsInitialized());
  XCTAssertEqual(0, audioDeviceModule->StartPlayout());
  XCTAssertTrue(audioDeviceModule->Playing());
}

- (void)stopPlayout {
  XCTAssertEqual(0, audioDeviceModule->StopPlayout());
  XCTAssertFalse(audioDeviceModule->Playing());
}

- (void)startRecording {
  XCTAssertFalse(audioDeviceModule->Recording());
  XCTAssertEqual(0, audioDeviceModule->InitRecording());
  XCTAssertTrue(audioDeviceModule->RecordingIsInitialized());
  XCTAssertEqual(0, audioDeviceModule->StartRecording());
  XCTAssertTrue(audioDeviceModule->Recording());
}

- (void)stopRecording {
  XCTAssertEqual(0, audioDeviceModule->StopRecording());
  XCTAssertFalse(audioDeviceModule->Recording());
}

- (NSURL *)fileURLForSampleRate:(int)sampleRate {
  XCTAssertTrue(sampleRate == 48000 || sampleRate == 44100 ||
                sampleRate == 16000);
  NSString *filename =
      [NSString stringWithFormat:@"audio_short%d", sampleRate / 1000];
  NSURL *url = [[NSBundle mainBundle] URLForResource:filename
                                       withExtension:@"pcm"];
  XCTAssertNotNil(url);

  return url;
}

#pragma mark - Tests

- (void)testConstructDestruct {
  XCTSkipIf(!_testEnabled);
  // Using the test fixture to create and destruct the audio device module.
}

- (void)testInitTerminate {
  XCTSkipIf(!_testEnabled);
  // Initialization is part of the test fixture.
  XCTAssertTrue(audioDeviceModule->Initialized());
  XCTAssertEqual(0, audioDeviceModule->Terminate());
  XCTAssertFalse(audioDeviceModule->Initialized());
}

// Tests that playout can be initiated, started and stopped. No audio callback
// is registered in this test.
- (void)testStartStopPlayout {
  XCTSkipIf(!_testEnabled);
  [self startPlayout];
  [self stopPlayout];
  [self startPlayout];
  [self stopPlayout];
}

// Tests that recording can be initiated, started and stopped. No audio callback
// is registered in this test.
- (void)testStartStopRecording {
  XCTSkipIf(!_testEnabled);
  [self startRecording];
  [self stopRecording];
  [self startRecording];
  [self stopRecording];
}
// Verify that calling StopPlayout() will leave us in an uninitialized state
// which will require a new call to InitPlayout(). This test does not call
// StartPlayout() while being uninitialized since doing so will hit a
// RTC_DCHECK.
- (void)testStopPlayoutRequiresInitToRestart {
  XCTSkipIf(!_testEnabled);
  XCTAssertEqual(0, audioDeviceModule->InitPlayout());
  XCTAssertEqual(0, audioDeviceModule->StartPlayout());
  XCTAssertEqual(0, audioDeviceModule->StopPlayout());
  XCTAssertFalse(audioDeviceModule->PlayoutIsInitialized());
}

// Verify that we can create two ADMs and start playing on the second ADM.
// Only the first active instance shall activate an audio session and the
// last active instance shall deactivate the audio session. The test does not
// explicitly verify correct audio session calls but instead focuses on
// ensuring that audio starts for both ADMs.
- (void)testStartPlayoutOnTwoInstances {
  XCTSkipIf(!_testEnabled);
  // Create and initialize a second/extra ADM instance. The default ADM is
  // created by the test harness.
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> secondAudioDeviceModule =
      webrtc::CreateAudioDeviceModule(webrtc::CreateEnvironment());
  XCTAssertNotEqual(secondAudioDeviceModule.get(), nullptr);
  XCTAssertEqual(0, secondAudioDeviceModule->Init());

  // Start playout for the default ADM but don't wait here. Instead use the
  // upcoming second stream for that. We set the same expectation on number
  // of callbacks as for the second stream.
  mock.expectNeedMorePlayData(^int32_t(const size_t nSamples,
                                       const size_t nBytesPerSample,
                                       const size_t nChannels,
                                       const uint32_t samplesPerSec,
                                       void *audioSamples,
                                       size_t &nSamplesOut,
                                       int64_t *elapsed_time_ms,
                                       int64_t *ntp_time_ms) {
    nSamplesOut = nSamples;
    XCTAssertEqual(nSamples, self.playoutParameters.frames_per_10ms_buffer());
    XCTAssertEqual(nBytesPerSample, kBytesPerSample);
    XCTAssertEqual(nChannels, self.playoutParameters.channels());
    XCTAssertEqual((int)samplesPerSec, self.playoutParameters.sample_rate());
    XCTAssertNotEqual((void *)NULL, audioSamples);

    return 0;
  });

  XCTAssertEqual(0, audioDeviceModule->RegisterAudioCallback(&mock));
  [self startPlayout];

  // Initialize playout for the second ADM. If all is OK, the second ADM shall
  // reuse the audio session activated when the first ADM started playing.
  // This call will also ensure that we avoid a problem related to initializing
  // two different audio unit instances back to back (see webrtc:5166 for
  // details).
  XCTAssertEqual(0, secondAudioDeviceModule->InitPlayout());
  XCTAssertTrue(secondAudioDeviceModule->PlayoutIsInitialized());

  // Start playout for the second ADM and verify that it starts as intended.
  // Passing this test ensures that initialization of the second audio unit
  // has been done successfully and that there is no conflict with the already
  // playing first ADM.
  XCTestExpectation *playoutExpectation =
      [self expectationWithDescription:@"NeedMorePlayoutData"];
  __block int num_callbacks = 0;

  MockAudioTransport mock2;
  mock2.expectNeedMorePlayData(^int32_t(const size_t nSamples,
                                        const size_t nBytesPerSample,
                                        const size_t nChannels,
                                        const uint32_t samplesPerSec,
                                        void *audioSamples,
                                        size_t &nSamplesOut,
                                        int64_t *elapsed_time_ms,
                                        int64_t *ntp_time_ms) {
    nSamplesOut = nSamples;
    XCTAssertEqual(nSamples, self.playoutParameters.frames_per_10ms_buffer());
    XCTAssertEqual(nBytesPerSample, kBytesPerSample);
    XCTAssertEqual(nChannels, self.playoutParameters.channels());
    XCTAssertEqual((int)samplesPerSec, self.playoutParameters.sample_rate());
    XCTAssertNotEqual((void *)NULL, audioSamples);
    if (++num_callbacks == kNumCallbacks) {
      [playoutExpectation fulfill];
    }

    return 0;
  });

  XCTAssertEqual(0, secondAudioDeviceModule->RegisterAudioCallback(&mock2));
  XCTAssertEqual(0, secondAudioDeviceModule->StartPlayout());
  XCTAssertTrue(secondAudioDeviceModule->Playing());
  [self waitForExpectationsWithTimeout:kTestTimeOutInSec handler:nil];
  [self stopPlayout];
  XCTAssertEqual(0, secondAudioDeviceModule->StopPlayout());
  XCTAssertFalse(secondAudioDeviceModule->Playing());
  XCTAssertFalse(secondAudioDeviceModule->PlayoutIsInitialized());

  XCTAssertEqual(0, secondAudioDeviceModule->Terminate());
}

// Start playout and verify that the native audio layer starts asking for real
// audio samples to play out using the NeedMorePlayData callback.
- (void)testStartPlayoutVerifyCallbacks {
  XCTSkipIf(!_testEnabled);
  XCTestExpectation *playoutExpectation =
      [self expectationWithDescription:@"NeedMorePlayoutData"];
  __block int num_callbacks = 0;
  mock.expectNeedMorePlayData(^int32_t(const size_t nSamples,
                                       const size_t nBytesPerSample,
                                       const size_t nChannels,
                                       const uint32_t samplesPerSec,
                                       void *audioSamples,
                                       size_t &nSamplesOut,
                                       int64_t *elapsed_time_ms,
                                       int64_t *ntp_time_ms) {
    nSamplesOut = nSamples;
    XCTAssertEqual(nSamples, self.playoutParameters.frames_per_10ms_buffer());
    XCTAssertEqual(nBytesPerSample, kBytesPerSample);
    XCTAssertEqual(nChannels, self.playoutParameters.channels());
    XCTAssertEqual((int)samplesPerSec, self.playoutParameters.sample_rate());
    XCTAssertNotEqual((void *)NULL, audioSamples);
    if (++num_callbacks == kNumCallbacks) {
      [playoutExpectation fulfill];
    }
    return 0;
  });

  XCTAssertEqual(0, audioDeviceModule->RegisterAudioCallback(&mock));

  [self startPlayout];
  [self waitForExpectationsWithTimeout:kTestTimeOutInSec handler:nil];
  [self stopPlayout];
}

// Start recording and verify that the native audio layer starts feeding real
// audio samples via the RecordedDataIsAvailable callback.
- (void)testStartRecordingVerifyCallbacks {
  XCTSkipIf(!_testEnabled);
  XCTestExpectation *recordExpectation =
      [self expectationWithDescription:@"RecordedDataIsAvailable"];
  __block int num_callbacks = 0;

  mock.expectRecordedDataIsAvailable(^(
      const void *audioSamples,
      const size_t nSamples,
      const size_t nBytesPerSample,
      const size_t nChannels,
      const uint32_t samplesPerSec,
      const uint32_t totalDelayMS,
      const int32_t clockDrift,
      const uint32_t currentMicLevel,
      const bool keyPressed,
      uint32_t &newMicLevel,
      std::optional<int64_t> /* estimatedCaptureTimeNS */) {
    XCTAssertNotEqual((void *)NULL, audioSamples);
    XCTAssertEqual(nSamples, self.recordParameters.frames_per_10ms_buffer());
    XCTAssertEqual(nBytesPerSample, kBytesPerSample);
    XCTAssertEqual(nChannels, self.recordParameters.channels());
    XCTAssertEqual((int)samplesPerSec, self.recordParameters.sample_rate());
    XCTAssertEqual(0, clockDrift);
    XCTAssertEqual(0u, currentMicLevel);
    XCTAssertFalse(keyPressed);
    if (++num_callbacks == kNumCallbacks) {
      [recordExpectation fulfill];
    }

    return 0;
  });

  XCTAssertEqual(0, audioDeviceModule->RegisterAudioCallback(&mock));
  [self startRecording];
  [self waitForExpectationsWithTimeout:kTestTimeOutInSec handler:nil];
  [self stopRecording];
}

// Start playout and recording (full-duplex audio) and verify that audio is
// active in both directions.
- (void)testStartPlayoutAndRecordingVerifyCallbacks {
  XCTSkipIf(!_testEnabled);
  XCTestExpectation *playoutExpectation =
      [self expectationWithDescription:@"NeedMorePlayoutData"];
  __block NSUInteger callbackCount = 0;

  XCTestExpectation *recordExpectation =
      [self expectationWithDescription:@"RecordedDataIsAvailable"];
  recordExpectation.expectedFulfillmentCount = kNumCallbacks;

  mock.expectNeedMorePlayData(^int32_t(const size_t nSamples,
                                       const size_t nBytesPerSample,
                                       const size_t nChannels,
                                       const uint32_t samplesPerSec,
                                       void *audioSamples,
                                       size_t &nSamplesOut,
                                       int64_t *elapsed_time_ms,
                                       int64_t *ntp_time_ms) {
    nSamplesOut = nSamples;
    XCTAssertEqual(nSamples, self.playoutParameters.frames_per_10ms_buffer());
    XCTAssertEqual(nBytesPerSample, kBytesPerSample);
    XCTAssertEqual(nChannels, self.playoutParameters.channels());
    XCTAssertEqual((int)samplesPerSec, self.playoutParameters.sample_rate());
    XCTAssertNotEqual((void *)NULL, audioSamples);
    if (callbackCount++ >= kNumCallbacks) {
      [playoutExpectation fulfill];
    }

    return 0;
  });

  mock.expectRecordedDataIsAvailable(^(
      const void *audioSamples,
      const size_t nSamples,
      const size_t nBytesPerSample,
      const size_t nChannels,
      const uint32_t samplesPerSec,
      const uint32_t totalDelayMS,
      const int32_t clockDrift,
      const uint32_t currentMicLevel,
      const bool keyPressed,
      uint32_t &newMicLevel,
      std::optional<int64_t> /* estimatedCaptureTimeNS */) {
    XCTAssertNotEqual((void *)NULL, audioSamples);
    XCTAssertEqual(nSamples, self.recordParameters.frames_per_10ms_buffer());
    XCTAssertEqual(nBytesPerSample, kBytesPerSample);
    XCTAssertEqual(nChannels, self.recordParameters.channels());
    XCTAssertEqual((int)samplesPerSec, self.recordParameters.sample_rate());
    XCTAssertEqual(0, clockDrift);
    XCTAssertEqual(0u, currentMicLevel);
    XCTAssertFalse(keyPressed);
    [recordExpectation fulfill];

    return 0;
  });

  XCTAssertEqual(0, audioDeviceModule->RegisterAudioCallback(&mock));
  [self startPlayout];
  [self startRecording];
  [self waitForExpectationsWithTimeout:kTestTimeOutInSec handler:nil];
  [self stopRecording];
  [self stopPlayout];
}

// Start playout and read audio from an external PCM file when the audio layer
// asks for data to play out. Real audio is played out in this test but it does
// not contain any explicit verification that the audio quality is perfect.
- (void)testRunPlayoutWithFileAsSource {
  XCTSkipIf(!_testEnabled);
  XCTAssertEqual(1u, playoutParameters.channels());

  // Using XCTestExpectation to count callbacks is very slow.
  XCTestExpectation *playoutExpectation =
      [self expectationWithDescription:@"NeedMorePlayoutData"];
  const int expectedCallbackCount = kFilePlayTimeInSec * kNumCallbacksPerSecond;
  __block int callbackCount = 0;

  NSURL *fileURL = [self fileURLForSampleRate:playoutParameters.sample_rate()];
  NSInputStream *inputStream = [[NSInputStream alloc] initWithURL:fileURL];

  mock.expectNeedMorePlayData(^int32_t(const size_t nSamples,
                                       const size_t nBytesPerSample,
                                       const size_t nChannels,
                                       const uint32_t samplesPerSec,
                                       void *audioSamples,
                                       size_t &nSamplesOut,
                                       int64_t *elapsed_time_ms,
                                       int64_t *ntp_time_ms) {
    [inputStream read:(uint8_t *)audioSamples
            maxLength:nSamples * nBytesPerSample * nChannels];
    nSamplesOut = nSamples;
    if (callbackCount++ == expectedCallbackCount) {
      [playoutExpectation fulfill];
    }

    return 0;
  });

  XCTAssertEqual(0, audioDeviceModule->RegisterAudioCallback(&mock));
  [self startPlayout];
  NSTimeInterval waitTimeout = kFilePlayTimeInSec * 2.0;
  [self waitForExpectationsWithTimeout:waitTimeout handler:nil];
  [self stopPlayout];
}

- (void)testDevices {
  XCTSkipIf(!_testEnabled);
  // Device enumeration is not supported. Verify fixed values only.
  XCTAssertEqual(1, audioDeviceModule->PlayoutDevices());
  XCTAssertEqual(1, audioDeviceModule->RecordingDevices());
}

// Start playout and recording and store recorded data in an intermediate FIFO
// buffer from which the playout side then reads its samples in the same order
// as they were stored. Under ideal circumstances, a callback sequence would
// look like: ...+-+-+-+-+-+-+-..., where '+' means 'packet recorded' and '-'
// means 'packet played'. Under such conditions, the FIFO would only contain
// one packet on average. However, under more realistic conditions, the size
// of the FIFO will vary more due to an unbalance between the two sides.
// This test tries to verify that the device maintains a balanced callback-
// sequence by running in loopback for ten seconds while measuring the size
// (max and average) of the FIFO. The size of the FIFO is increased by the
// recording side and decreased by the playout side.
// TODO(henrika): tune the final test parameters after running tests on several
// different devices.
- (void)testRunPlayoutAndRecordingInFullDuplex {
  XCTSkipIf(!_testEnabled);
  XCTAssertEqual(recordParameters.channels(), playoutParameters.channels());
  XCTAssertEqual(recordParameters.sample_rate(),
                 playoutParameters.sample_rate());

  XCTestExpectation *playoutExpectation =
      [self expectationWithDescription:@"NeedMorePlayoutData"];
  __block NSUInteger playoutCallbacks = 0;
  NSUInteger expectedPlayoutCallbacks =
      kFullDuplexTimeInSec * kNumCallbacksPerSecond;

  // FIFO queue and measurements
  NSMutableArray *fifoBuffer = [NSMutableArray arrayWithCapacity:20];
  __block NSUInteger fifoMaxSize = 0;
  __block NSUInteger fifoTotalWrittenElements = 0;
  __block NSUInteger fifoWriteCount = 0;

  mock.expectRecordedDataIsAvailable(^(
      const void *audioSamples,
      const size_t nSamples,
      const size_t nBytesPerSample,
      const size_t nChannels,
      const uint32_t samplesPerSec,
      const uint32_t totalDelayMS,
      const int32_t clockDrift,
      const uint32_t currentMicLevel,
      const bool keyPressed,
      uint32_t &newMicLevel,
      std::optional<int64_t> /* estimatedCaptureTimeNS */) {
    if (fifoWriteCount++ < kNumIgnoreFirstCallbacks) {
      return 0;
    }

    NSData *data =
        [NSData dataWithBytes:audioSamples
                       length:nSamples * nBytesPerSample * nChannels];
    @synchronized(fifoBuffer) {
      [fifoBuffer addObject:data];
      fifoMaxSize = MAX(fifoMaxSize, fifoBuffer.count);
      fifoTotalWrittenElements += fifoBuffer.count;
    }

    return 0;
  });

  mock.expectNeedMorePlayData(^int32_t(const size_t nSamples,
                                       const size_t nBytesPerSample,
                                       const size_t nChannels,
                                       const uint32_t samplesPerSec,
                                       void *audioSamples,
                                       size_t &nSamplesOut,
                                       int64_t *elapsed_time_ms,
                                       int64_t *ntp_time_ms) {
    nSamplesOut = nSamples;
    NSData *data;
    @synchronized(fifoBuffer) {
      data = fifoBuffer.firstObject;
      if (data) {
        [fifoBuffer removeObjectAtIndex:0];
      }
    }

    if (data) {
      memcpy(audioSamples, (char *)data.bytes, data.length);
    } else {
      memset(audioSamples, 0, nSamples * nBytesPerSample * nChannels);
    }

    if (playoutCallbacks++ == expectedPlayoutCallbacks) {
      [playoutExpectation fulfill];
    }
    return 0;
  });

  XCTAssertEqual(0, audioDeviceModule->RegisterAudioCallback(&mock));
  [self startRecording];
  [self startPlayout];
  NSTimeInterval waitTimeout = kFullDuplexTimeInSec * 2.0;
  [self waitForExpectationsWithTimeout:waitTimeout handler:nil];

  size_t fifoAverageSize = (fifoTotalWrittenElements == 0) ? 0.0 :
                                                             0.5 +
          (double)fifoTotalWrittenElements /
              (fifoWriteCount - kNumIgnoreFirstCallbacks);

  [self stopPlayout];
  [self stopRecording];
  XCTAssertLessThan(fifoAverageSize, 10u);
  XCTAssertLessThan(fifoMaxSize, 20u);
}

@end
