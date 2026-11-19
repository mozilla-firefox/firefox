/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/video_capture.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/time_delta.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "api/video/video_sink_interface.h"
#include "modules/video_capture/video_capture_defines.h"
#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/checks.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "system_wrappers/include/clock.h"
#include "test/frame_utils.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using ::testing::Ge;

const int kTimeOut = 5000;
#ifdef WEBRTC_MAC
static const int kTestHeight = 288;
static const int kTestWidth = 352;
static const int kTestFramerate = 30;
#endif

class TestVideoCaptureCallback : public VideoSinkInterface<webrtc::VideoFrame> {
 public:
  explicit TestVideoCaptureCallback(Clock& clock)
      : clock_(clock),
        last_render_time_ms_(0),
        incoming_frames_(0),
        timing_warnings_(0),
        rotate_frame_(kVideoRotation_0) {}

  ~TestVideoCaptureCallback() override {
    if (timing_warnings_ > 0)
      printf("No of timing warnings %d\n", timing_warnings_);
  }

  void OnFrame(const VideoFrame& videoFrame) override {
    MutexLock lock(&capture_lock_);
    int height = videoFrame.height();
    int width = videoFrame.width();
#if defined(WEBRTC_ANDROID) && WEBRTC_ANDROID
    // Android camera frames may be rotated depending on test device
    // orientation.
    EXPECT_TRUE(height == capability_.height || height == capability_.width);
    EXPECT_TRUE(width == capability_.width || width == capability_.height);
#else
    EXPECT_EQ(height, capability_.height);
    EXPECT_EQ(width, capability_.width);
    EXPECT_EQ(rotate_frame_, videoFrame.rotation());
#endif
    // RenderTimstamp should be the time now.
    EXPECT_TRUE(videoFrame.render_time_ms() >=
                    clock_.TimeInMilliseconds() - 30 &&
                videoFrame.render_time_ms() <= clock_.TimeInMilliseconds());

    if ((videoFrame.render_time_ms() >
             last_render_time_ms_ + (1000 * 1.1) / capability_.maxFPS &&
         last_render_time_ms_ > 0) ||
        (videoFrame.render_time_ms() <
             last_render_time_ms_ + (1000 * 0.9) / capability_.maxFPS &&
         last_render_time_ms_ > 0)) {
      timing_warnings_++;
    }

    incoming_frames_++;
    last_render_time_ms_ = videoFrame.render_time_ms();
    last_frame_ = videoFrame.video_frame_buffer();
  }

  void SetExpectedCapability(VideoCaptureCapability capability) {
    MutexLock lock(&capture_lock_);
    capability_ = capability;
    incoming_frames_ = 0;
    last_render_time_ms_ = 0;
  }
  int incoming_frames() {
    MutexLock lock(&capture_lock_);
    return incoming_frames_;
  }

  int timing_warnings() {
    MutexLock lock(&capture_lock_);
    return timing_warnings_;
  }
  VideoCaptureCapability capability() {
    MutexLock lock(&capture_lock_);
    return capability_;
  }

  bool CompareLastFrame(const VideoFrame& frame) {
    MutexLock lock(&capture_lock_);
    return test::FrameBufsEqual(last_frame_, frame.video_frame_buffer());
  }

  void SetExpectedCaptureRotation(VideoRotation rotation) {
    MutexLock lock(&capture_lock_);
    rotate_frame_ = rotation;
  }

 private:
  Mutex capture_lock_;
  VideoCaptureCapability capability_;
  Clock& clock_;
  int64_t last_render_time_ms_;
  int incoming_frames_;
  int timing_warnings_;
  scoped_refptr<webrtc::VideoFrameBuffer> last_frame_;
  VideoRotation rotate_frame_;
};

class VideoCaptureTest : public ::testing::Test {
 public:
  VideoCaptureTest()
      : clock_(*Clock::GetRealTimeClock()), number_of_devices_(0) {}

  void SetUp() override {
    device_info_.reset(VideoCaptureFactory::CreateDeviceInfo());
    RTC_DCHECK(device_info_.get());
    number_of_devices_ = device_info_->NumberOfDevices();
    ASSERT_GT(number_of_devices_, 0u);
  }

  scoped_refptr<VideoCaptureModule> OpenVideoCaptureDevice(
      unsigned int device,
      VideoSinkInterface<webrtc::VideoFrame>* callback) {
    char device_name[256];
    char unique_name[256];

    EXPECT_EQ(0, device_info_->GetDeviceName(device, device_name, 256,
                                             unique_name, 256));

    scoped_refptr<VideoCaptureModule> module(
        VideoCaptureFactory::Create(unique_name));
    if (module.get() == nullptr)
      return nullptr;

    EXPECT_FALSE(module->CaptureStarted());

    module->RegisterCaptureDataCallback(callback);
    return module;
  }

  void StartCapture(VideoCaptureModule* capture_module,
                    VideoCaptureCapability capability) {
    ASSERT_EQ(0, capture_module->StartCapture(capability));
    EXPECT_TRUE(capture_module->CaptureStarted());

    VideoCaptureCapability resulting_capability;
    EXPECT_EQ(0, capture_module->CaptureSettings(resulting_capability));
    EXPECT_EQ(capability.width, resulting_capability.width);
    EXPECT_EQ(capability.height, resulting_capability.height);
  }

  Clock& clock_;
  std::unique_ptr<VideoCaptureModule::DeviceInfo> device_info_;
  unsigned int number_of_devices_;
};

#ifdef WEBRTC_MAC
// Currently fails on Mac 64-bit, see
// https://bugs.chromium.org/p/webrtc/issues/detail?id=5406
#define MAYBE_CreateDelete DISABLED_CreateDelete
#else
#define MAYBE_CreateDelete CreateDelete
#endif
TEST_F(VideoCaptureTest, MAYBE_CreateDelete) {
  for (int i = 0; i < 5; ++i) {
    int64_t start_time = clock_.TimeInMilliseconds();
    TestVideoCaptureCallback capture_observer(clock_);
    scoped_refptr<VideoCaptureModule> module(
        OpenVideoCaptureDevice(0, &capture_observer));
    ASSERT_TRUE(module.get() != nullptr);

    VideoCaptureCapability capability;
#ifndef WEBRTC_MAC
    device_info_->GetCapability(module->CurrentDeviceName(), 0, capability);
#else
    capability.width = kTestWidth;
    capability.height = kTestHeight;
    capability.maxFPS = kTestFramerate;
    capability.videoType = VideoType::kUnknown;
#endif
    capture_observer.SetExpectedCapability(capability);
    ASSERT_NO_FATAL_FAILURE(StartCapture(module.get(), capability));

    // Less than 4s to start the camera.
    EXPECT_LE(clock_.TimeInMilliseconds() - start_time, 4000);

    // Make sure 5 frames are captured.
    EXPECT_THAT(WaitUntil([&] { return capture_observer.incoming_frames(); },
                          Ge(5), {.timeout = TimeDelta::Millis(kTimeOut)}),
                IsRtcOk());

    int64_t stop_time = clock_.TimeInMilliseconds();
    EXPECT_EQ(0, module->StopCapture());
    EXPECT_FALSE(module->CaptureStarted());

    // Less than 3s to stop the camera.
    EXPECT_LE(clock_.TimeInMilliseconds() - stop_time, 3000);
  }
}

#ifdef WEBRTC_MAC
// Currently fails on Mac 64-bit, see
// https://bugs.chromium.org/p/webrtc/issues/detail?id=5406
#define MAYBE_Capabilities DISABLED_Capabilities
#else
#define MAYBE_Capabilities Capabilities
#endif
TEST_F(VideoCaptureTest, MAYBE_Capabilities) {
  TestVideoCaptureCallback capture_observer(clock_);

  scoped_refptr<VideoCaptureModule> module(
      OpenVideoCaptureDevice(0, &capture_observer));
  ASSERT_TRUE(module.get() != nullptr);

  int number_of_capabilities =
      device_info_->NumberOfCapabilities(module->CurrentDeviceName());
  EXPECT_GT(number_of_capabilities, 0);
  // Key is <width>x<height>, value is vector of maxFPS values at that
  // resolution.
  typedef std::map<std::string, std::vector<int> > FrameRatesByResolution;
  FrameRatesByResolution frame_rates_by_resolution;
  for (int i = 0; i < number_of_capabilities; ++i) {
    VideoCaptureCapability capability;
    EXPECT_EQ(0, device_info_->GetCapability(module->CurrentDeviceName(), i,
                                             capability));
    StringBuilder sb;
    sb << capability.width << "x" << capability.height;
    std::string resolution = sb.Release();
    frame_rates_by_resolution[resolution].push_back(capability.maxFPS);

    // Since Android presents so many resolution/FPS combinations and the test
    // runner imposes a timeout, we only actually start the capture and test
    // that a frame was captured for 2 frame-rates at each resolution.
    if (frame_rates_by_resolution[resolution].size() > 2)
      continue;

    capture_observer.SetExpectedCapability(capability);
    ASSERT_NO_FATAL_FAILURE(StartCapture(module.get(), capability));
    // Make sure at least one frame is captured.
    EXPECT_THAT(WaitUntil([&] { return capture_observer.incoming_frames(); },
                          Ge(1), {.timeout = TimeDelta::Millis(kTimeOut)}),
                IsRtcOk());

    EXPECT_EQ(0, module->StopCapture());
  }

#if defined(WEBRTC_ANDROID) && WEBRTC_ANDROID
  // There's no reason for this to _necessarily_ be true, but in practice all
  // Android devices this test runs on in fact do support multiple capture
  // resolutions and multiple frame-rates per captured resolution, so we assert
  // this fact here as a regression-test against the time that we only noticed a
  // single frame-rate per resolution (bug 2974).  If this test starts being run
  // on devices for which this is untrue (e.g. Nexus4) then the following should
  // probably be wrapped in a base::android::BuildInfo::model()/device() check.
  EXPECT_GT(frame_rates_by_resolution.size(), 1U);
  for (FrameRatesByResolution::const_iterator it =
           frame_rates_by_resolution.begin();
       it != frame_rates_by_resolution.end(); ++it) {
    EXPECT_GT(it->second.size(), 1U) << it->first;
  }
#endif  // WEBRTC_ANDROID
}

// NOTE: flaky, crashes sometimes.
// http://code.google.com/p/webrtc/issues/detail?id=777
TEST_F(VideoCaptureTest, DISABLED_TestTwoCameras) {
  if (number_of_devices_ < 2) {
    printf("There are not two cameras available. Aborting test. \n");
    return;
  }

  TestVideoCaptureCallback capture_observer1(clock_);
  scoped_refptr<VideoCaptureModule> module1(
      OpenVideoCaptureDevice(0, &capture_observer1));
  ASSERT_TRUE(module1.get() != nullptr);
  VideoCaptureCapability capability1;
#ifndef WEBRTC_MAC
  device_info_->GetCapability(module1->CurrentDeviceName(), 0, capability1);
#else
  capability1.width = kTestWidth;
  capability1.height = kTestHeight;
  capability1.maxFPS = kTestFramerate;
  capability1.videoType = VideoType::kUnknown;
#endif
  capture_observer1.SetExpectedCapability(capability1);

  TestVideoCaptureCallback capture_observer2(clock_);
  scoped_refptr<VideoCaptureModule> module2(
      OpenVideoCaptureDevice(1, &capture_observer2));
  ASSERT_TRUE(module1.get() != nullptr);

  VideoCaptureCapability capability2;
#ifndef WEBRTC_MAC
  device_info_->GetCapability(module2->CurrentDeviceName(), 0, capability2);
#else
  capability2.width = kTestWidth;
  capability2.height = kTestHeight;
  capability2.maxFPS = kTestFramerate;
  capability2.videoType = VideoType::kUnknown;
#endif
  capture_observer2.SetExpectedCapability(capability2);

  ASSERT_NO_FATAL_FAILURE(StartCapture(module1.get(), capability1));
  ASSERT_NO_FATAL_FAILURE(StartCapture(module2.get(), capability2));
  EXPECT_THAT(WaitUntil([&] { return capture_observer1.incoming_frames(); },
                        Ge(5), {.timeout = TimeDelta::Millis(kTimeOut)}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return capture_observer2.incoming_frames(); },
                        Ge(5), {.timeout = TimeDelta::Millis(kTimeOut)}),
              IsRtcOk());
  EXPECT_EQ(0, module2->StopCapture());
  EXPECT_EQ(0, module1->StopCapture());
}

#ifdef WEBRTC_MAC
// No VideoCaptureImpl on Mac.
#define MAYBE_ConcurrentAccess DISABLED_ConcurrentAccess
#else
#define MAYBE_ConcurrentAccess ConcurrentAccess
#endif
TEST_F(VideoCaptureTest, MAYBE_ConcurrentAccess) {
  TestVideoCaptureCallback capture_observer1(clock_);
  scoped_refptr<VideoCaptureModule> module1(
      OpenVideoCaptureDevice(0, &capture_observer1));
  ASSERT_TRUE(module1.get() != nullptr);
  VideoCaptureCapability capability;
  device_info_->GetCapability(module1->CurrentDeviceName(), 0, capability);
  capture_observer1.SetExpectedCapability(capability);

  TestVideoCaptureCallback capture_observer2(clock_);
  scoped_refptr<VideoCaptureModule> module2(
      OpenVideoCaptureDevice(0, &capture_observer2));
  ASSERT_TRUE(module2.get() != nullptr);
  capture_observer2.SetExpectedCapability(capability);

  // Starting module1 should work.
  ASSERT_NO_FATAL_FAILURE(StartCapture(module1.get(), capability));
  EXPECT_THAT(WaitUntil([&] { return capture_observer1.incoming_frames(); },
                        Ge(5), {.timeout = TimeDelta::Millis(kTimeOut)}),
              IsRtcOk());

  // When module1 is stopped, starting module2 for the same device should work.
  EXPECT_EQ(0, module1->StopCapture());
  ASSERT_NO_FATAL_FAILURE(StartCapture(module2.get(), capability));
  EXPECT_THAT(WaitUntil([&] { return capture_observer2.incoming_frames(); },
                        Ge(5), {.timeout = TimeDelta::Millis(kTimeOut)}),
              IsRtcOk());

  EXPECT_EQ(0, module2->StopCapture());
}

}  // namespace
}  // namespace webrtc
