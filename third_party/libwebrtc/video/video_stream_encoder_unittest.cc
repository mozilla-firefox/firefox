/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "video/video_stream_encoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/adaptation/resource.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/fec_controller_override.h"
#include "api/field_trials.h"
#include "api/field_trials_view.h"
#include "api/location.h"
#include "api/make_ref_counted.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/test/mock_fec_controller_override.h"
#include "api/test/mock_video_encoder.h"
#include "api/test/mock_video_encoder_factory.h"
#include "api/test/rtc_error_matchers.h"
#include "api/test/time_controller.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video/corruption_detection/frame_instrumentation_data.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/render_resolution.h"
#include "api/video/resolution.h"
#include "api/video/video_adaptation_counters.h"
#include "api/video/video_adaptation_reason.h"
#include "api/video/video_adapter.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_bitrate_allocator.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video/video_rotation.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video/video_stream_encoder_settings.h"
#include "api/video/video_timing.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/vp8_frame_buffer_controller.h"
#include "api/video_codecs/vp8_temporal_layers_factory.h"
#include "api/video_track_source_constraints.h"
#include "call/adaptation/test/fake_adaptation_constraint.h"
#include "call/adaptation/test/fake_resource.h"
#include "call/adaptation/video_source_restrictions.h"
#include "call/video_send_stream.h"
#include "common_video/h264/h264_common.h"
#include "media/engine/webrtc_video_engine.h"
#include "modules/video_coding/codecs/av1/libaom_av1_encoder.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "modules/video_coding/codecs/vp9/svc_config.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/utility/simulcast_rate_allocator.h"
#include "modules/video_coding/utility/vp8_constants.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/experiments/encoder_info_settings.h"
#include "rtc_base/experiments/quality_scaling_experiment.h"
#include "rtc_base/experiments/rate_control_settings.h"
#include "rtc_base/logging.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/encoder_settings.h"
#include "test/fake_encoder.h"
#include "test/frame_forwarder.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mappable_native_buffer.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/video_encoder_nullable_proxy_factory.h"
#include "test/video_encoder_proxy_factory.h"
#include "test/wait_until.h"
#include "video/adaptation/overuse_frame_detector.h"
#include "video/config/video_encoder_config.h"
#include "video/encoder_bitrate_adjuster.h"
#include "video/frame_cadence_adapter.h"
#include "video/send_statistics_proxy.h"
#include "video/video_stream_encoder_interface.h"
#include "video/video_stream_encoder_observer.h"

namespace webrtc {

using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Matcher;
using ::testing::Mock;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::StrictMock;

namespace {
constexpr int kMinPixelsPerFrame = 320 * 180;
constexpr int kQpLow = 1;
constexpr int kQpHigh = 2;
constexpr int kMinFramerateFps = 2;
constexpr int kMinBalancedFramerateFps = 7;
constexpr TimeDelta kFrameTimeout = TimeDelta::Millis(100);
constexpr size_t kMaxPayloadLength = 1440;
constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(1000);
constexpr DataRate kLowTargetBitrate = DataRate::KilobitsPerSec(100);
constexpr DataRate kStartBitrate = DataRate::KilobitsPerSec(600);
constexpr DataRate kSimulcastTargetBitrate = DataRate::KilobitsPerSec(3150);
constexpr int kMaxInitialFramedrop = 4;
constexpr int kDefaultFramerate = 30;
constexpr int64_t kFrameIntervalMs = kNumMillisecsPerSec / kDefaultFramerate;
constexpr int64_t kProcessIntervalMs = 1000;
const VideoEncoder::ResolutionBitrateLimits
    kEncoderBitrateLimits540p(960 * 540, 100 * 1000, 100 * 1000, 2000 * 1000);
const VideoEncoder::ResolutionBitrateLimits
    kEncoderBitrateLimits720p(1280 * 720, 200 * 1000, 200 * 1000, 4000 * 1000);

uint8_t kOptimalSps[] = {0,    0,    0,    1,    H264::NaluType::kSps,
                         0x00, 0x00, 0x03, 0x03, 0xF4,
                         0x05, 0x03, 0xC7, 0xE0, 0x1B,
                         0x41, 0x10, 0x8D, 0x00};

constexpr uint8_t kCodedFrameVp8Qp25[] = {
    0x10, 0x02, 0x00, 0x9d, 0x01, 0x2a, 0x10, 0x00, 0x10, 0x00,
    0x02, 0x47, 0x08, 0x85, 0x85, 0x88, 0x85, 0x84, 0x88, 0x0c,
    0x82, 0x00, 0x0c, 0x0d, 0x60, 0x00, 0xfe, 0xfc, 0x5c, 0xd0};

#ifdef RTC_ENABLE_H265
// Default value from encoder_info_settings.cc
constexpr DataRate kDefaultH265Bitrate180p = DataRate::KilobitsPerSec(150);
#endif

VideoFrame CreateSimpleNV12Frame() {
  return VideoFrame::Builder()
      .set_video_frame_buffer(make_ref_counted<NV12Buffer>(
          /*width=*/16, /*height=*/16))
      .build();
}

void PassAFrame(
    TaskQueueBase* encoder_queue,
    FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback,
    int64_t ntp_time_ms) {
  encoder_queue->PostTask([video_stream_encoder_callback, ntp_time_ms] {
    video_stream_encoder_callback->OnFrame(Timestamp::Millis(ntp_time_ms),
                                           false, CreateSimpleNV12Frame());
  });
}

class TestBuffer : public I420Buffer {
 public:
  TestBuffer(Event* event, int width, int height)
      : I420Buffer(width, height), event_(event) {}

 private:
  friend class RefCountedObject<TestBuffer>;
  ~TestBuffer() override {
    if (event_)
      event_->Set();
  }
  Event* const event_;
};

// A fake native buffer that can't be converted to I420. Upon scaling, it
// produces another FakeNativeBuffer.
class FakeNativeBuffer : public VideoFrameBuffer {
 public:
  FakeNativeBuffer(Event* event, int width, int height)
      : event_(event), width_(width), height_(height) {}
  VideoFrameBuffer::Type type() const override { return Type::kNative; }
  int width() const override { return width_; }
  int height() const override { return height_; }
  scoped_refptr<I420BufferInterface> ToI420() override { return nullptr; }
  scoped_refptr<VideoFrameBuffer> CropAndScale(int offset_x,
                                               int offset_y,
                                               int crop_width,
                                               int crop_height,
                                               int scaled_width,
                                               int scaled_height) override {
    return make_ref_counted<FakeNativeBuffer>(nullptr, scaled_width,
                                              scaled_height);
  }

 private:
  friend class RefCountedObject<FakeNativeBuffer>;
  ~FakeNativeBuffer() override {
    if (event_)
      event_->Set();
  }
  Event* const event_;
  const int width_;
  const int height_;
};

// A fake native buffer that is backed by an NV12 buffer.
class FakeNV12NativeBuffer : public VideoFrameBuffer {
 public:
  FakeNV12NativeBuffer(Event* event, int width, int height)
      : nv12_buffer_(NV12Buffer::Create(width, height)), event_(event) {}

  VideoFrameBuffer::Type type() const override { return Type::kNative; }
  int width() const override { return nv12_buffer_->width(); }
  int height() const override { return nv12_buffer_->height(); }
  scoped_refptr<I420BufferInterface> ToI420() override {
    return nv12_buffer_->ToI420();
  }
  scoped_refptr<VideoFrameBuffer> GetMappedFrameBuffer(
      std::span<VideoFrameBuffer::Type> types) override {
    if (absl::c_find(types, Type::kNV12) != types.end()) {
      return nv12_buffer_;
    }
    return nullptr;
  }
  const NV12BufferInterface* GetNV12() const { return nv12_buffer_.get(); }

 private:
  friend class RefCountedObject<FakeNV12NativeBuffer>;
  ~FakeNV12NativeBuffer() override {
    if (event_)
      event_->Set();
  }
  scoped_refptr<NV12Buffer> nv12_buffer_;
  Event* const event_;
};

class CpuOveruseDetectorProxy : public OveruseFrameDetector {
 public:
  CpuOveruseDetectorProxy(const Environment& env,
                          CpuOveruseMetricsObserver* metrics_observer)
      : OveruseFrameDetector(env, metrics_observer),
        last_target_framerate_fps_(-1) {}
  ~CpuOveruseDetectorProxy() override {}

  void OnTargetFramerateUpdated(int framerate_fps) override {
    MutexLock lock(&lock_);
    last_target_framerate_fps_ = framerate_fps;
    OveruseFrameDetector::OnTargetFramerateUpdated(framerate_fps);
    framerate_updated_ = true;
  }

  int GetLastTargetFramerate() {
    MutexLock lock(&lock_);
    return last_target_framerate_fps_;
  }

  CpuOveruseOptions GetOptions() { return options_; }

  // Returns whether OnTargetFramerateUpdated() has been called since the
  // previous call to this method, and clears the flag.
  bool ConsumeFramerateUpdated() {
    MutexLock lock(&lock_);
    bool framerate_updated = framerate_updated_;
    framerate_updated_ = false;
    return framerate_updated;
  }

 private:
  Mutex lock_;
  int last_target_framerate_fps_ RTC_GUARDED_BY(lock_);
  bool framerate_updated_ RTC_GUARDED_BY(lock_) = false;
};

auto WantsFps(Matcher<int> fps_matcher) {
  return Field("max_framerate_fps", &VideoSinkWants::max_framerate_fps,
               fps_matcher);
}

auto WantsMaxPixels(Matcher<int> max_pixel_matcher) {
  return Field("max_pixel_count", &VideoSinkWants::max_pixel_count,
               AllOf(max_pixel_matcher, Gt(0)));
}

auto ResolutionMax() {
  return AllOf(WantsMaxPixels(Eq(std::numeric_limits<int>::max())),
               Field("target_pixel_count", &VideoSinkWants::target_pixel_count,
                     Eq(std::nullopt)));
}

auto FpsMax() {
  return WantsFps(Eq(kDefaultFramerate));
}

auto FpsUnlimited() {
  return WantsFps(Eq(std::numeric_limits<int>::max()));
}

auto FpsMatchesResolutionMax(Matcher<int> fps_matcher) {
  return AllOf(WantsFps(fps_matcher), ResolutionMax());
}

auto FpsMaxResolutionMatches(Matcher<int> pixel_matcher) {
  return AllOf(FpsMax(), WantsMaxPixels(pixel_matcher));
}

auto FpsUnlimitedResolutionMatches(Matcher<int> pixel_matcher) {
  return AllOf(FpsUnlimited(), WantsMaxPixels(pixel_matcher));
}

auto FpsMaxResolutionMax() {
  return AllOf(FpsMax(), ResolutionMax());
}

auto UnlimitedSinkWants() {
  return AllOf(FpsUnlimited(), ResolutionMax());
}

auto FpsInRangeForPixelsInBalanced(int last_frame_pixels) {
  Matcher<int> fps_range_matcher;

  if (last_frame_pixels <= 320 * 240) {
    fps_range_matcher = AllOf(Ge(7), Le(10));
  } else if (last_frame_pixels <= 480 * 360) {
    fps_range_matcher = AllOf(Ge(10), Le(15));
  } else if (last_frame_pixels <= 640 * 480) {
    fps_range_matcher = Ge(15);
  } else {
    fps_range_matcher = Eq(kDefaultFramerate);
  }
  return Field("max_framerate_fps", &VideoSinkWants::max_framerate_fps,
               fps_range_matcher);
}

auto FpsEqResolutionEqTo(const VideoSinkWants& other_wants) {
  return AllOf(WantsFps(Eq(other_wants.max_framerate_fps)),
               WantsMaxPixels(Eq(other_wants.max_pixel_count)));
}

auto FpsMaxResolutionLt(const VideoSinkWants& other_wants) {
  return AllOf(FpsMax(), WantsMaxPixels(Lt(other_wants.max_pixel_count)));
}

auto FpsUnlimitedResolutionLt(const VideoSinkWants& other_wants) {
  return AllOf(FpsUnlimited(), WantsMaxPixels(Lt(other_wants.max_pixel_count)));
}

auto FpsMaxResolutionGt(const VideoSinkWants& other_wants) {
  return AllOf(FpsMax(), WantsMaxPixels(Gt(other_wants.max_pixel_count)));
}

auto FpsUnlimitedResolutionGt(const VideoSinkWants& other_wants) {
  return AllOf(FpsUnlimited(), WantsMaxPixels(Gt(other_wants.max_pixel_count)));
}

auto FpsLtResolutionEq(const VideoSinkWants& other_wants) {
  return AllOf(WantsFps(Lt(other_wants.max_framerate_fps)),
               WantsMaxPixels(Eq(other_wants.max_pixel_count)));
}

auto FpsGtResolutionEq(const VideoSinkWants& other_wants) {
  return AllOf(WantsFps(Gt(other_wants.max_framerate_fps)),
               WantsMaxPixels(Eq(other_wants.max_pixel_count)));
}

auto FpsEqResolutionLt(const VideoSinkWants& other_wants) {
  return AllOf(WantsFps(Eq(other_wants.max_framerate_fps)),
               WantsMaxPixels(Lt(other_wants.max_pixel_count)));
}

auto FpsEqResolutionGt(const VideoSinkWants& other_wants) {
  return AllOf(WantsFps(Eq(other_wants.max_framerate_fps)),
               WantsMaxPixels(Gt(other_wants.max_pixel_count)));
}

class VideoStreamEncoderUnderTest : public VideoStreamEncoder {
 public:
  VideoStreamEncoderUnderTest(
      const Environment& env,
      TimeController* time_controller,
      std::unique_ptr<FrameCadenceAdapterInterface> cadence_adapter,
      std::unique_ptr<TaskQueueBase, TaskQueueDeleter> encoder_queue,
      // Points at `encoder_queue`, which is owned by VideoStreamEncoder. Passed
      // separately because it has been moved by the time the members below are
      // initialized.
      TaskQueueBase* encoder_queue_ptr,
      // Owned by the caller for the same reason: the base class constructor
      // runs before the members of this class are initialized.
      scoped_refptr<FakeResource> fake_cpu_resource,
      scoped_refptr<FakeResource> fake_quality_resource,
      FakeAdaptationConstraint* fake_adaptation_constraint,
      SendStatisticsProxy* stats_proxy,
      VideoStreamEncoderSettings settings,
      VideoStreamEncoder::BitrateAllocationCallbackType
          allocation_callback_type,
      int num_cores,
      EncoderSwitchRequestCallback encoder_switch_request_callback = nullptr)
      : VideoStreamEncoder(
            env,
            num_cores,
            stats_proxy,
            std::move(settings),
            std::unique_ptr<OveruseFrameDetector>(
                overuse_detector_proxy_ =
                    new CpuOveruseDetectorProxy(env, stats_proxy)),
            std::move(cadence_adapter),
            std::move(encoder_queue),
            allocation_callback_type,
            nullptr,  // encoder_selector
            std::move(encoder_switch_request_callback),
            AdaptationInjectionsForTest{
                .resources = {{fake_quality_resource,
                               VideoAdaptationReason::kQuality},
                              {fake_cpu_resource, VideoAdaptationReason::kCpu}},
                .constraints = {fake_adaptation_constraint}}),
        time_controller_(time_controller),
        encoder_queue_ptr_(encoder_queue_ptr),
        fake_cpu_resource_(std::move(fake_cpu_resource)),
        fake_quality_resource_(std::move(fake_quality_resource)) {}

  void SetSourceAndWaitForRestrictionsUpdated(
      VideoSourceInterface<VideoFrame>* source,
      const DegradationPreference& degradation_preference) {
    SetSource(source, degradation_preference);
    // Restrictions are updated on the encoder queue.
    WaitUntilTaskQueueIsIdle();
  }

  void SetSourceAndWaitForFramerateUpdated(
      VideoSourceInterface<VideoFrame>* source,
      const DegradationPreference& degradation_preference) {
    SetSource(source, degradation_preference);
    // The target framerate is updated on the encoder queue.
    WaitUntilTaskQueueIsIdle();
  }

  void OnBitrateUpdatedAndWaitForManagedResources(DataRate target_bitrate,
                                                  DataRate link_allocation,
                                                  uint8_t fraction_lost,
                                                  int64_t round_trip_time_ms,
                                                  double cwnd_reduce_ratio) {
    OnBitrateUpdated(target_bitrate, link_allocation, fraction_lost,
                     round_trip_time_ms, cwnd_reduce_ratio);
    // Bitrate is updated on the encoder queue.
    WaitUntilTaskQueueIsIdle();
  }

  // This is used as a synchronisation mechanism, to make sure that the
  // encoder queue is not blocked before we start sending it frames.
  void WaitUntilTaskQueueIsIdle() {
    time_controller_->AdvanceTime(TimeDelta::Zero());
  }

  // Triggers resource usage measurements on the fake CPU resource.
  void TriggerCpuOveruse() {
    encoder_queue_ptr_->PostTask([this] {
      fake_cpu_resource_->SetUsageState(ResourceUsageState::kOveruse);
    });
    WaitUntilTaskQueueIsIdle();
  }

  void TriggerCpuUnderuse() {
    encoder_queue_ptr_->PostTask([this] {
      fake_cpu_resource_->SetUsageState(ResourceUsageState::kUnderuse);
    });
    WaitUntilTaskQueueIsIdle();
  }

  // Triggers resource usage measurements on the fake quality resource.
  void TriggerQualityLow() {
    encoder_queue_ptr_->PostTask([this] {
      fake_quality_resource_->SetUsageState(ResourceUsageState::kOveruse);
    });
    WaitUntilTaskQueueIsIdle();
  }

  void TriggerQualityHigh() {
    encoder_queue_ptr_->PostTask([this] {
      fake_quality_resource_->SetUsageState(ResourceUsageState::kUnderuse);
    });
    WaitUntilTaskQueueIsIdle();
  }

  TimeController* const time_controller_;
  TaskQueueBase* const encoder_queue_ptr_;
  CpuOveruseDetectorProxy* overuse_detector_proxy_;
  scoped_refptr<FakeResource> fake_cpu_resource_;
  scoped_refptr<FakeResource> fake_quality_resource_;
};

// Simulates simulcast behavior and makes highest stream resolutions divisible
// by 4.
class CroppingVideoStreamFactory
    : public VideoEncoderConfig::VideoStreamFactoryInterface {
 public:
  CroppingVideoStreamFactory() {}

 private:
  std::vector<VideoStream> CreateEncoderStreams(
      const FieldTrialsView& /*field_trials*/,
      int frame_width,
      int frame_height,
      const VideoEncoderConfig& encoder_config) override {
    std::vector<VideoStream> streams = test::CreateVideoStreams(
        frame_width - frame_width % 4, frame_height - frame_height % 4,
        encoder_config);
    return streams;
  }
};

class AdaptingFrameForwarder : public test::FrameForwarder {
 public:
  explicit AdaptingFrameForwarder(TimeController* time_controller)
      : time_controller_(time_controller), adaptation_enabled_(false) {}
  ~AdaptingFrameForwarder() override {}

  void set_adaptation_enabled(bool enabled) {
    MutexLock lock(&mutex_);
    adaptation_enabled_ = enabled;
  }

  bool adaption_enabled() const {
    MutexLock lock(&mutex_);
    return adaptation_enabled_;
  }

  // The "last wants" is a snapshot of the previous VideoSinkWants where
  // the resolution or frame rate was different than it is currently. If
  // something else is modified, such as encoder resolutions, but the resolution
  // and frame rate stays the same, last wants is not updated.
  VideoSinkWants last_wants() const {
    MutexLock lock(&mutex_);
    return last_wants_;
  }

  std::optional<int> last_sent_width() const { return last_width_; }
  std::optional<int> last_sent_height() const { return last_height_; }

  void IncomingCapturedFrame(const VideoFrame& video_frame) override {
    RTC_DCHECK(time_controller_->GetMainThread()->IsCurrent());
    time_controller_->AdvanceTime(TimeDelta::Zero());

    int cropped_width = 0;
    int cropped_height = 0;
    int out_width = 0;
    int out_height = 0;
    if (adaption_enabled()) {
      RTC_DLOG(LS_INFO) << "IncomingCapturedFrame: AdaptFrameResolution()"
                        << "w=" << video_frame.width()
                        << "h=" << video_frame.height();
      if (adapter_.AdaptFrameResolution(
              video_frame.width(), video_frame.height(),
              video_frame.timestamp_us() * 1000, &cropped_width,
              &cropped_height, &out_width, &out_height)) {
        VideoFrame adapted_frame =
            VideoFrame::Builder()
                .set_video_frame_buffer(make_ref_counted<TestBuffer>(
                    nullptr, out_width, out_height))
                .set_ntp_time_ms(video_frame.ntp_time_ms())
                .set_timestamp_ms(video_frame.timestamp_us() * 1000)
                .set_rotation(kVideoRotation_0)
                .build();
        if (video_frame.has_update_rect()) {
          adapted_frame.set_update_rect(
              video_frame.update_rect().ScaleWithFrame(
                  video_frame.width(), video_frame.height(), 0, 0,
                  video_frame.width(), video_frame.height(), out_width,
                  out_height));
        }
        test::FrameForwarder::IncomingCapturedFrame(adapted_frame);
        last_width_.emplace(adapted_frame.width());
        last_height_.emplace(adapted_frame.height());
      } else {
        last_width_ = std::nullopt;
        last_height_ = std::nullopt;
      }
    } else {
      RTC_DLOG(LS_INFO) << "IncomingCapturedFrame: adaptation not enabled";
      test::FrameForwarder::IncomingCapturedFrame(video_frame);
      last_width_.emplace(video_frame.width());
      last_height_.emplace(video_frame.height());
    }
  }

  void OnOutputFormatRequest(int width, int height) {
    std::optional<std::pair<int, int>> target_aspect_ratio =
        std::make_pair(width, height);
    std::optional<int> max_pixel_count = width * height;
    std::optional<int> max_fps;
    adapter_.OnOutputFormatRequest(target_aspect_ratio, max_pixel_count,
                                   max_fps);
  }

  void AddOrUpdateSink(VideoSinkInterface<VideoFrame>* sink,
                       const VideoSinkWants& wants) override {
    MutexLock lock(&mutex_);
    VideoSinkWants prev_wants = sink_wants_locked();
    bool did_adapt =
        prev_wants.max_pixel_count != wants.max_pixel_count ||
        prev_wants.target_pixel_count != wants.target_pixel_count ||
        prev_wants.max_framerate_fps != wants.max_framerate_fps;
    if (did_adapt) {
      last_wants_ = prev_wants;
    }
    adapter_.OnSinkWants(wants);
    test::FrameForwarder::AddOrUpdateSinkLocked(sink, wants);
  }

  void RequestRefreshFrame() override { ++refresh_frames_requested_; }

  TimeController* const time_controller_;
  VideoAdapter adapter_;
  bool adaptation_enabled_ RTC_GUARDED_BY(mutex_);
  VideoSinkWants last_wants_ RTC_GUARDED_BY(mutex_);
  std::optional<int> last_width_;
  std::optional<int> last_height_;
  int refresh_frames_requested_{0};
};

// TODO(nisse): Mock only VideoStreamEncoderObserver.
class MockableSendStatisticsProxy : public SendStatisticsProxy {
 public:
  MockableSendStatisticsProxy(const Environment& env,
                              const VideoSendStream::Config& config,
                              VideoEncoderConfig::ContentType content_type)
      : SendStatisticsProxy(env, config, content_type) {}

  VideoSendStream::Stats GetStats() override {
    MutexLock lock(&lock_);
    if (mock_stats_)
      return *mock_stats_;
    return SendStatisticsProxy::GetStats();
  }

  int GetInputFrameRate() const override {
    MutexLock lock(&lock_);
    if (mock_stats_)
      return mock_stats_->input_frame_rate;
    return SendStatisticsProxy::GetInputFrameRate();
  }
  void SetMockStats(const VideoSendStream::Stats& stats) {
    MutexLock lock(&lock_);
    mock_stats_.emplace(stats);
  }

  void ResetMockStats() {
    MutexLock lock(&lock_);
    mock_stats_.reset();
  }

  void SetDroppedFrameCallback(std::function<void(DropReason)> callback) {
    on_frame_dropped_ = std::move(callback);
  }

 private:
  void OnFrameDropped(DropReason reason) override {
    SendStatisticsProxy::OnFrameDropped(reason);
    if (on_frame_dropped_)
      on_frame_dropped_(reason);
  }

  mutable Mutex lock_;
  std::optional<VideoSendStream::Stats> mock_stats_ RTC_GUARDED_BY(lock_);
  std::function<void(DropReason)> on_frame_dropped_;
};

class SimpleVideoStreamEncoderFactory {
 public:
  class AdaptedVideoStreamEncoder : public VideoStreamEncoder {
   public:
    using VideoStreamEncoder::VideoStreamEncoder;
    ~AdaptedVideoStreamEncoder() override { Stop(); }
  };

  class MockFakeEncoder : public test::FakeEncoder {
   public:
    using FakeEncoder::FakeEncoder;
    MOCK_METHOD(CodecSpecificInfo,
                EncodeHook,
                (EncodedImage & encoded_image,
                 scoped_refptr<EncodedImageBuffer> buffer),
                (override));
    MOCK_METHOD(VideoEncoder::EncoderInfo,
                GetEncoderInfo,
                (),
                (const, override));
  };

  explicit SimpleVideoStreamEncoderFactory(absl::string_view field_trials = "")
      : field_trials_(CreateTestFieldTrials(field_trials)) {
    encoder_settings_.encoder_factory = &encoder_factory_;
    encoder_settings_.bitrate_allocator_factory =
        bitrate_allocator_factory_.get();
  }

  std::unique_ptr<AdaptedVideoStreamEncoder> CreateWithEncoderQueue(
      std::unique_ptr<FrameCadenceAdapterInterface> zero_hertz_adapter,
      std::unique_ptr<TaskQueueBase, TaskQueueDeleter> encoder_queue) {
    auto result = std::make_unique<AdaptedVideoStreamEncoder>(
        env_,
        /*number_of_cores=*/1,
        /*stats_proxy=*/stats_proxy_.get(), std::move(encoder_settings_),
        std::make_unique<CpuOveruseDetectorProxy>(env_,
                                                  /*stats_proxy=*/nullptr),
        std::move(zero_hertz_adapter), std::move(encoder_queue),
        VideoStreamEncoder::BitrateAllocationCallbackType::
            kVideoLayersAllocation);
    result->SetSink(&sink_, /*rotation_applied=*/false);
    return result;
  }

  std::unique_ptr<AdaptedVideoStreamEncoder> Create(
      std::unique_ptr<FrameCadenceAdapterInterface> zero_hertz_adapter,
      TaskQueueBase** encoder_queue_ptr = nullptr) {
    auto encoder_queue =
        time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
            "EncoderQueue", TaskQueueFactory::Priority::kNormal);
    if (encoder_queue_ptr)
      *encoder_queue_ptr = encoder_queue.get();
    return CreateWithEncoderQueue(std::move(zero_hertz_adapter),
                                  std::move(encoder_queue));
  }

  void DepleteTaskQueues() { time_controller_.AdvanceTime(TimeDelta::Zero()); }
  MockFakeEncoder& GetMockFakeEncoder() { return mock_fake_encoder_; }

  GlobalSimulatedTimeController* GetTimeController() {
    return &time_controller_;
  }

  const Environment& env() { return env_; }

 private:
  class NullEncoderSink : public VideoStreamEncoderInterface::EncoderSink {
   public:
    ~NullEncoderSink() override = default;
    void OnEncoderConfigurationChanged(
        std::vector<VideoStream> streams,
        bool is_svc,
        VideoEncoderConfig::ContentType content_type,
        int min_transmit_bitrate_bps) override {}
    void OnVideoLayersAllocationUpdated(
        VideoLayersAllocation allocation) override {}
    Result OnEncodedImage(
        const EncodedImage& encoded_image,
        const CodecSpecificInfo* codec_specific_info) override {
      return Result(EncodedImageCallback::Result::OK);
    }
    void OnFrameDropped(uint32_t rtp_timestamp,
                        int spatial_id,
                        bool is_end_of_temporal_unit) override {}
  };

  FieldTrials field_trials_ = CreateTestFieldTrials();
  GlobalSimulatedTimeController time_controller_{Timestamp::Zero()};
  Environment env_ = CreateTestEnvironment(
      {.field_trials = &field_trials_, .time = &time_controller_});
  std::unique_ptr<MockableSendStatisticsProxy> stats_proxy_ =
      std::make_unique<MockableSendStatisticsProxy>(
          env_,
          VideoSendStream::Config(nullptr),
          VideoEncoderConfig::ContentType::kRealtimeVideo);
  std::unique_ptr<VideoBitrateAllocatorFactory> bitrate_allocator_factory_ =
      CreateBuiltinVideoBitrateAllocatorFactory();
  VideoStreamEncoderSettings encoder_settings_{
      VideoEncoder::Capabilities(false)};
  MockFakeEncoder mock_fake_encoder_{env_};
  test::VideoEncoderProxyFactory encoder_factory_{&mock_fake_encoder_};
  NullEncoderSink sink_;
};

class MockFrameCadenceAdapter : public FrameCadenceAdapterInterface {
 public:
  MOCK_METHOD(void, Initialize, (Callback * callback), (override));
  MOCK_METHOD(void,
              SetZeroHertzModeEnabled,
              (std::optional<ZeroHertzModeParams>),
              (override));
  MOCK_METHOD(void, OnFrame, (const VideoFrame&), (override));
  MOCK_METHOD(std::optional<uint32_t>, GetInputFrameRateFps, (), (override));
  MOCK_METHOD(void,
              UpdateLayerQualityConvergence,
              (size_t spatial_index, bool converged),
              (override));
  MOCK_METHOD(void,
              UpdateLayerStatus,
              (size_t spatial_index, bool enabled),
              (override));
  MOCK_METHOD(void,
              UpdateVideoSourceRestrictions,
              (std::optional<double>),
              (override));
  MOCK_METHOD(void, ProcessKeyFrameRequest, (), (override));
};

class MockEncoderSelector
    : public VideoEncoderFactory::EncoderSelectorInterface {
 public:
  MOCK_METHOD(void,
              OnCurrentEncoder,
              (const SdpVideoFormat& format),
              (override));
  MOCK_METHOD(std::optional<SdpVideoFormat>,
              OnAvailableBitrate,
              (const DataRate& rate),
              (override));
  MOCK_METHOD(std::optional<SdpVideoFormat>,
              OnResolutionChange,
              (const RenderResolution& resolution),
              (override));
  MOCK_METHOD(std::optional<SdpVideoFormat>, OnEncoderBroken, (), (override));
};

class MockVideoSourceInterface : public VideoSourceInterface<VideoFrame> {
 public:
  MOCK_METHOD(void,
              AddOrUpdateSink,
              (VideoSinkInterface<VideoFrame>*, const VideoSinkWants&),
              (override));
  MOCK_METHOD(void, RemoveSink, (VideoSinkInterface<VideoFrame>*), (override));
  MOCK_METHOD(void, RequestRefreshFrame, (), (override));
};

}  // namespace

class VideoStreamEncoderTest : public ::testing::Test {
 public:
  static constexpr TimeDelta kDefaultTimeout = TimeDelta::Seconds(1);

  VideoStreamEncoderTest()
      : video_send_config_(VideoSendStream::Config(nullptr)),
        codec_width_(320),
        codec_height_(240),
        max_framerate_(kDefaultFramerate),
        fake_encoder_(env_),
        encoder_factory_(&fake_encoder_),
        stats_proxy_(new MockableSendStatisticsProxy(
            env_,
            video_send_config_,
            VideoEncoderConfig::ContentType::kRealtimeVideo)),
        sink_(&time_controller_, &fake_encoder_),
        encoder_switch_request_callback_(nullptr) {}

  void SetUp() override {
    metrics::Reset();
    video_send_config_ = VideoSendStream::Config(nullptr);
    video_send_config_.encoder_settings.encoder_factory = &encoder_factory_;
    video_send_config_.encoder_settings.bitrate_allocator_factory =
        &bitrate_allocator_factory_;
    video_send_config_.rtp.payload_name = "FAKE";
    video_send_config_.rtp.payload_type = 125;

    VideoEncoderConfig video_encoder_config;
    test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
    EXPECT_EQ(1u, video_encoder_config.simulcast_layers.size());
    video_encoder_config.simulcast_layers[0].num_temporal_layers = 1;
    video_encoder_config_ = video_encoder_config.Copy();

    ConfigureEncoder(std::move(video_encoder_config));
  }

  FieldTrials SetFieldTrial(absl::string_view key, absl::string_view value) {
    FieldTrials copy(field_trials_);
    copy.Set(key, value);
    return copy;
  }

  void ConfigureEncoder(
      VideoEncoderConfig video_encoder_config,
      VideoStreamEncoder::BitrateAllocationCallbackType
          allocation_callback_type =
              VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
      int num_cores = 1,
      const FieldTrialsView* field_trials = nullptr) {
    if (video_stream_encoder_)
      video_stream_encoder_->Stop();

    auto encoder_queue = env_.task_queue_factory().CreateTaskQueue(
        "EncoderQueue", TaskQueueFactory::Priority::kNormal);
    TaskQueueBase* encoder_queue_ptr = encoder_queue.get();
    std::unique_ptr<FrameCadenceAdapterInterface> cadence_adapter =
        FrameCadenceAdapterInterface::Create(
            time_controller_.GetClock(), encoder_queue_ptr,
            /*metronome=*/nullptr, /*worker_queue=*/nullptr,
            field_trials ? *field_trials : field_trials_);
    if (field_trials) {
      auto factory = EnvironmentFactory(env_);
      factory.Set(field_trials->CreateCopy());
      env_ = factory.Create();
    }

    VideoStreamEncoderSettings settings = video_send_config_.encoder_settings;
    video_stream_encoder_ = std::make_unique<VideoStreamEncoderUnderTest>(
        env_, &time_controller_, std::move(cadence_adapter),
        std::move(encoder_queue), encoder_queue_ptr,
        FakeResource::Create("FakeResource[CPU]"),
        FakeResource::Create("FakeResource[QP]"), &fake_adaptation_constraint_,
        stats_proxy_.get(), std::move(settings), allocation_callback_type,
        num_cores, std::move(encoder_switch_request_callback_));
    video_stream_encoder_->SetSink(&sink_, /*rotation_applied=*/false);
    video_stream_encoder_->SetSource(&video_source_,
                                     DegradationPreference::MAINTAIN_FRAMERATE);
    video_stream_encoder_->SetStartBitrate(kTargetBitrate.bps());
    video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                            kMaxPayloadLength, nullptr);
    video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  }

  void ResetEncoder(
      const std::string& payload_name,
      size_t num_streams,
      size_t num_temporal_layers,
      unsigned char num_spatial_layers,
      bool screenshare,
      std::optional<int> max_frame_rate = kDefaultFramerate,
      VideoStreamEncoder::BitrateAllocationCallbackType
          allocation_callback_type =
              VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
      int num_cores = 1,
      const FieldTrialsView* field_trials = nullptr) {
    video_send_config_.rtp.payload_name = payload_name;

    VideoEncoderConfig video_encoder_config;
    test::FillEncoderConfiguration(PayloadStringToCodecType(payload_name),
                                   num_streams, &video_encoder_config);
    for (auto& layer : video_encoder_config.simulcast_layers) {
      layer.num_temporal_layers = num_temporal_layers;
      if (max_frame_rate) {
        layer.max_framerate = *max_frame_rate;
      }
    }
    video_encoder_config.max_bitrate_bps =
        num_streams == 1 ? kTargetBitrate.bps() : kSimulcastTargetBitrate.bps();
    video_encoder_config.content_type =
        screenshare ? VideoEncoderConfig::ContentType::kScreen
                    : VideoEncoderConfig::ContentType::kRealtimeVideo;
    if (payload_name == "VP9") {
      VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
      vp9_settings.numberOfSpatialLayers = num_spatial_layers;
      vp9_settings.automaticResizeOn = num_spatial_layers <= 1;
      video_encoder_config.encoder_specific_settings =
          make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
              vp9_settings);
    }
    ConfigureEncoder(std::move(video_encoder_config), allocation_callback_type,
                     num_cores, field_trials);
  }

  VideoFrame CreateFrame(int64_t ntp_time_ms, Event* destruction_event) const {
    return VideoFrame::Builder()
        .set_video_frame_buffer(make_ref_counted<TestBuffer>(
            destruction_event, codec_width_, codec_height_))
        .set_ntp_time_ms(ntp_time_ms)
        .set_timestamp_ms(ntp_time_ms)
        .set_rotation(kVideoRotation_0)
        .build();
  }

  VideoFrame CreateFrameWithUpdatedPixel(int64_t ntp_time_ms,
                                         Event* destruction_event,
                                         int offset_x) const {
    return VideoFrame::Builder()
        .set_video_frame_buffer(make_ref_counted<TestBuffer>(
            destruction_event, codec_width_, codec_height_))
        .set_ntp_time_ms(ntp_time_ms)
        .set_timestamp_ms(ntp_time_ms)
        .set_rotation(kVideoRotation_0)
        .set_update_rect(VideoFrame::UpdateRect{
            .offset_x = offset_x, .offset_y = 0, .width = 1, .height = 1})
        .build();
  }

  VideoFrame CreateFrame(int64_t ntp_time_ms, int width, int height) const {
    auto buffer = make_ref_counted<TestBuffer>(nullptr, width, height);
    I420Buffer::SetBlack(buffer.get());
    return VideoFrame::Builder()
        .set_video_frame_buffer(std::move(buffer))
        .set_ntp_time_ms(ntp_time_ms)
        .set_timestamp_ms(ntp_time_ms)
        .set_rotation(kVideoRotation_0)
        .build();
  }

  VideoFrame CreateNV12Frame(int64_t ntp_time_ms, int width, int height) const {
    return VideoFrame::Builder()
        .set_video_frame_buffer(NV12Buffer::Create(width, height))
        .set_ntp_time_ms(ntp_time_ms)
        .set_timestamp_ms(ntp_time_ms)
        .set_rotation(kVideoRotation_0)
        .build();
  }

  VideoFrame CreateFakeNativeFrame(int64_t ntp_time_ms,
                                   Event* destruction_event,
                                   int width,
                                   int height) const {
    return VideoFrame::Builder()
        .set_video_frame_buffer(make_ref_counted<FakeNativeBuffer>(
            destruction_event, width, height))
        .set_ntp_time_ms(ntp_time_ms)
        .set_timestamp_ms(ntp_time_ms)
        .set_rotation(kVideoRotation_0)
        .build();
  }

  VideoFrame CreateFakeNV12NativeFrame(int64_t ntp_time_ms,
                                       Event* destruction_event,
                                       int width,
                                       int height) const {
    return VideoFrame::Builder()
        .set_video_frame_buffer(make_ref_counted<FakeNV12NativeBuffer>(
            destruction_event, width, height))
        .set_ntp_time_ms(ntp_time_ms)
        .set_timestamp_ms(ntp_time_ms)
        .set_rotation(kVideoRotation_0)
        .build();
  }

  VideoFrame CreateFakeNativeFrame(int64_t ntp_time_ms,
                                   Event* destruction_event) const {
    return CreateFakeNativeFrame(ntp_time_ms, destruction_event, codec_width_,
                                 codec_height_);
  }

  void WaitForEncodedFrame(int64_t expected_ntp_time) {
    sink_.WaitForEncodedFrame(expected_ntp_time);
    AdvanceTime(TimeDelta::Seconds(1) / max_framerate_);
  }

  bool TimedWaitForEncodedFrame(int64_t expected_ntp_time, TimeDelta timeout) {
    bool ok = sink_.TimedWaitForEncodedFrame(expected_ntp_time, timeout);
    AdvanceTime(TimeDelta::Seconds(1) / max_framerate_);
    return ok;
  }

  void WaitForEncodedFrame(uint32_t expected_width, uint32_t expected_height) {
    sink_.WaitForEncodedFrame(expected_width, expected_height);
    AdvanceTime(TimeDelta::Seconds(1) / max_framerate_);
  }

  void ExpectDroppedFrame() {
    sink_.ExpectDroppedFrame();
    AdvanceTime(TimeDelta::Seconds(1) / max_framerate_);
  }

  bool WaitForFrame(TimeDelta timeout) {
    bool ok = sink_.WaitForFrame(timeout);
    AdvanceTime(TimeDelta::Seconds(1) / max_framerate_);
    return ok;
  }

  class TestEncoder : public test::FakeEncoder {
   public:
    explicit TestEncoder(const Environment& env) : FakeEncoder(env) {}

    VideoEncoder::EncoderInfo GetEncoderInfo() const override {
      MutexLock lock(&local_mutex_);
      EncoderInfo info = FakeEncoder::GetEncoderInfo();
      if (initialized_ == EncoderState::kInitialized) {
        if (quality_scaling_) {
          info.scaling_settings = VideoEncoder::ScalingSettings(
              kQpLow, kQpHigh, kMinPixelsPerFrame);
        }
        info.is_hardware_accelerated = is_hardware_accelerated_;
        for (size_t i = 0; i < kMaxSpatialLayers; ++i) {
          if (temporal_layers_supported_[i]) {
            info.fps_allocation[i].clear();
            int num_layers = temporal_layers_supported_[i].value() ? 2 : 1;
            for (int tid = 0; tid < num_layers; ++tid)
              info.fps_allocation[i].push_back(255 / (num_layers - tid));
          }
        }
      }

      info.resolution_bitrate_limits = resolution_bitrate_limits_;
      info.requested_resolution_alignment = requested_resolution_alignment_;
      info.apply_alignment_to_all_simulcast_layers =
          apply_alignment_to_all_simulcast_layers_;
      info.max_pixels_per_frame = max_pixels_per_frame_;
      info.preferred_pixel_formats = preferred_pixel_formats_;
      info.enable_cpu_overuse_detection = enable_cpu_overuse_detection_;
      if (is_qp_trusted_.has_value()) {
        info.is_qp_trusted = is_qp_trusted_;
      }
      if (has_trusted_rate_controller_.has_value()) {
        info.has_trusted_rate_controller = *has_trusted_rate_controller_;
      }
      return info;
    }

    int32_t RegisterEncodeCompleteCallback(
        EncodedImageCallback* callback) override {
      MutexLock lock(&local_mutex_);
      encoded_image_callback_ = callback;
      return FakeEncoder::RegisterEncodeCompleteCallback(callback);
    }

    void ContinueEncode() { continue_encode_event_.Set(); }

    void CheckLastTimeStampsMatch(int64_t ntp_time_ms,
                                  uint32_t timestamp) const {
      MutexLock lock(&local_mutex_);
      EXPECT_EQ(timestamp_, timestamp);
      EXPECT_EQ(ntp_time_ms_, ntp_time_ms);
    }

    void SetQualityScaling(bool b) {
      MutexLock lock(&local_mutex_);
      quality_scaling_ = b;
    }

    void SetRequestedResolutionAlignment(
        uint32_t requested_resolution_alignment) {
      MutexLock lock(&local_mutex_);
      requested_resolution_alignment_ = requested_resolution_alignment;
    }

    void SetApplyAlignmentToAllSimulcastLayers(bool b) {
      MutexLock lock(&local_mutex_);
      apply_alignment_to_all_simulcast_layers_ = b;
    }

    void SetMaxPixelsPerFrame(std::optional<size_t> max_pixels_per_frame) {
      MutexLock lock(&local_mutex_);
      max_pixels_per_frame_ = max_pixels_per_frame;
    }

    void SetIsHardwareAccelerated(bool is_hardware_accelerated) {
      MutexLock lock(&local_mutex_);
      is_hardware_accelerated_ = is_hardware_accelerated;
    }

    void SetEnableCpuOveruseDetection(bool enable) {
      MutexLock lock(&local_mutex_);
      enable_cpu_overuse_detection_ = enable;
    }

    void SetTemporalLayersSupported(size_t spatial_idx, bool supported) {
      RTC_DCHECK_LT(spatial_idx, kMaxSpatialLayers);
      MutexLock lock(&local_mutex_);
      temporal_layers_supported_[spatial_idx] = supported;
    }

    void SetResolutionBitrateLimits(
        std::vector<ResolutionBitrateLimits> thresholds) {
      MutexLock lock(&local_mutex_);
      resolution_bitrate_limits_ = thresholds;
    }

    void ForceInitEncodeFailure(bool force_failure) {
      MutexLock lock(&local_mutex_);
      force_init_encode_failed_ = force_failure;
    }

    void SimulateOvershoot(double rate_factor) {
      MutexLock lock(&local_mutex_);
      rate_factor_ = rate_factor;
    }

    uint32_t GetLastFramerate() const {
      MutexLock lock(&local_mutex_);
      return last_framerate_;
    }

    VideoFrame::UpdateRect GetLastUpdateRect() const {
      MutexLock lock(&local_mutex_);
      return last_update_rect_;
    }

    const std::vector<VideoFrameType>& LastFrameTypes() const {
      MutexLock lock(&local_mutex_);
      return last_frame_types_;
    }

    void InjectFrame(const VideoFrame& input_image, bool keyframe) {
      const std::vector<VideoFrameType> frame_type = {
          keyframe ? VideoFrameType::kVideoFrameKey
                   : VideoFrameType::kVideoFrameDelta};
      {
        MutexLock lock(&local_mutex_);
        last_frame_types_ = frame_type;
      }
      FakeEncoder::Encode(input_image, &frame_type);
    }

    void InjectEncodedImage(const EncodedImage& image,
                            const CodecSpecificInfo* codec_specific_info) {
      MutexLock lock(&local_mutex_);
      encoded_image_callback_->OnEncodedImage(image, codec_specific_info);
    }

    void SetEncodedImageData(
        scoped_refptr<EncodedImageBufferInterface> encoded_image_data) {
      MutexLock lock(&local_mutex_);
      encoded_image_data_ = encoded_image_data;
    }

    void ForceDropFrames(bool drop) {
      MutexLock lock(&local_mutex_);
      drop_frames_ = drop;
    }

    void ExpectNullFrame() {
      MutexLock lock(&local_mutex_);
      expect_null_frame_ = true;
    }

    std::optional<VideoEncoder::RateControlParameters>
    GetAndResetLastRateControlSettings() {
      auto settings = last_rate_control_settings_;
      last_rate_control_settings_.reset();
      return settings;
    }

    int GetLastInputWidth() const {
      MutexLock lock(&local_mutex_);
      return last_input_width_;
    }

    int GetLastInputHeight() const {
      MutexLock lock(&local_mutex_);
      return last_input_height_;
    }

    std::optional<VideoFrameBuffer::Type> GetLastInputPixelFormat() {
      MutexLock lock(&local_mutex_);
      return last_input_pixel_format_;
    }

    int GetNumSetRates() const {
      MutexLock lock(&local_mutex_);
      return num_set_rates_;
    }

    int GetNumEncodes() const {
      MutexLock lock(&local_mutex_);
      return num_encodes_;
    }

    void SetPreferredPixelFormats(
        absl::InlinedVector<VideoFrameBuffer::Type, kMaxPreferredPixelFormats>
            pixel_formats) {
      MutexLock lock(&local_mutex_);
      preferred_pixel_formats_ = std::move(pixel_formats);
    }

    void SetIsQpTrusted(std::optional<bool> trusted) {
      MutexLock lock(&local_mutex_);
      is_qp_trusted_ = trusted;
    }

    void SetIsRateControlTrusted(bool trusted) {
      MutexLock lock(&local_mutex_);
      has_trusted_rate_controller_ = trusted;
    }

    VideoCodecComplexity LastEncoderComplexity() {
      MutexLock lock(&local_mutex_);
      return last_encoder_complexity_;
    }

   private:
    int32_t Encode(const VideoFrame& input_image,
                   const std::vector<VideoFrameType>* frame_types) override {
      {
        MutexLock lock(&local_mutex_);
        num_encodes_++;
        if (drop_frames_) {
          if (encoded_image_callback_) {
            encoded_image_callback_->OnFrameDropped(
                input_image.rtp_timestamp(), /*spatial_id=*/0,
                /*is_end_of_temporal_unit=*/true);
          }
          return WEBRTC_VIDEO_CODEC_OK;
        }
        if (expect_null_frame_) {
          EXPECT_EQ(input_image.rtp_timestamp(), 0u);
          EXPECT_EQ(input_image.width(), 1);
          last_frame_types_ = *frame_types;
          expect_null_frame_ = false;
        } else {
          EXPECT_GT(input_image.rtp_timestamp(), timestamp_);
          EXPECT_GT(input_image.ntp_time_ms(), ntp_time_ms_);
          EXPECT_EQ(input_image.rtp_timestamp(),
                    input_image.ntp_time_ms() * 90);
        }

        timestamp_ = input_image.rtp_timestamp();
        ntp_time_ms_ = input_image.ntp_time_ms();
        last_input_width_ = input_image.width();
        last_input_height_ = input_image.height();
        last_update_rect_ = input_image.update_rect();
        last_frame_types_ = *frame_types;
        last_input_pixel_format_ = input_image.video_frame_buffer()->type();
      }
      int32_t result = FakeEncoder::Encode(input_image, frame_types);
      return result;
    }

    CodecSpecificInfo EncodeHook(
        EncodedImage& encoded_image,
        scoped_refptr<EncodedImageBuffer> buffer) override {
      CodecSpecificInfo codec_specific;
      {
        MutexLock lock(&mutex_);
        codec_specific.codecType = config_.codecType;
      }
      MutexLock lock(&local_mutex_);
      if (encoded_image_data_) {
        encoded_image.SetEncodedData(encoded_image_data_);
      }
      return codec_specific;
    }

    int32_t InitEncode(const VideoCodec* config,
                       const Settings& settings) override {
      int res = FakeEncoder::InitEncode(config, settings);

      MutexLock lock(&local_mutex_);
      EXPECT_EQ(initialized_, EncoderState::kUninitialized);

      if (config->codecType == kVideoCodecVP8) {
        // Simulate setting up temporal layers, in order to validate the life
        // cycle of these objects.
        Vp8TemporalLayersFactory factory;
        frame_buffer_controller_ =
            factory.Create(env_, *config, settings, &fec_controller_override_);
      }

      last_encoder_complexity_ = config->GetVideoEncoderComplexity();

      if (force_init_encode_failed_) {
        initialized_ = EncoderState::kInitializationFailed;
        return -1;
      }

      initialized_ = EncoderState::kInitialized;
      return res;
    }

    int32_t Release() override {
      MutexLock lock(&local_mutex_);
      EXPECT_NE(initialized_, EncoderState::kUninitialized);
      initialized_ = EncoderState::kUninitialized;
      return FakeEncoder::Release();
    }

    void SetRates(const RateControlParameters& parameters) override {
      MutexLock lock(&local_mutex_);
      num_set_rates_++;
      VideoBitrateAllocation adjusted_rate_allocation;
      for (size_t si = 0; si < kMaxSpatialLayers; ++si) {
        for (size_t ti = 0; ti < kMaxTemporalStreams; ++ti) {
          if (parameters.bitrate.HasBitrate(si, ti)) {
            adjusted_rate_allocation.SetBitrate(
                si, ti,
                static_cast<uint32_t>(parameters.bitrate.GetBitrate(si, ti) *
                                      rate_factor_));
          }
        }
      }
      last_framerate_ = static_cast<uint32_t>(parameters.framerate_fps + 0.5);
      last_rate_control_settings_ = parameters;
      RateControlParameters adjusted_paramters = parameters;
      adjusted_paramters.bitrate = adjusted_rate_allocation;
      FakeEncoder::SetRates(adjusted_paramters);
    }

    mutable Mutex local_mutex_;
    enum class EncoderState {
      kUninitialized,
      kInitializationFailed,
      kInitialized
    } initialized_ RTC_GUARDED_BY(local_mutex_) = EncoderState::kUninitialized;
    Event continue_encode_event_;
    uint32_t timestamp_ RTC_GUARDED_BY(local_mutex_) = 0;
    int64_t ntp_time_ms_ RTC_GUARDED_BY(local_mutex_) = 0;
    int last_input_width_ RTC_GUARDED_BY(local_mutex_) = 0;
    int last_input_height_ RTC_GUARDED_BY(local_mutex_) = 0;
    bool quality_scaling_ RTC_GUARDED_BY(local_mutex_) = true;
    uint32_t requested_resolution_alignment_ RTC_GUARDED_BY(local_mutex_) = 1;
    bool apply_alignment_to_all_simulcast_layers_ RTC_GUARDED_BY(local_mutex_) =
        false;
    std::optional<size_t> max_pixels_per_frame_ RTC_GUARDED_BY(local_mutex_);
    bool is_hardware_accelerated_ RTC_GUARDED_BY(local_mutex_) = false;
    bool enable_cpu_overuse_detection_ RTC_GUARDED_BY(local_mutex_) = true;
    scoped_refptr<EncodedImageBufferInterface> encoded_image_data_
        RTC_GUARDED_BY(local_mutex_);
    std::unique_ptr<Vp8FrameBufferController> frame_buffer_controller_
        RTC_GUARDED_BY(local_mutex_);
    std::optional<bool>
        temporal_layers_supported_[kMaxSpatialLayers] RTC_GUARDED_BY(
            local_mutex_);
    bool force_init_encode_failed_ RTC_GUARDED_BY(local_mutex_) = false;
    double rate_factor_ RTC_GUARDED_BY(local_mutex_) = 1.0;
    uint32_t last_framerate_ RTC_GUARDED_BY(local_mutex_) = 0;
    std::optional<VideoEncoder::RateControlParameters>
        last_rate_control_settings_;
    VideoFrame::UpdateRect last_update_rect_ RTC_GUARDED_BY(
        local_mutex_) = {.offset_x = 0, .offset_y = 0, .width = 0, .height = 0};
    std::vector<VideoFrameType> last_frame_types_;
    bool expect_null_frame_ = false;
    bool drop_frames_ RTC_GUARDED_BY(local_mutex_) = false;
    EncodedImageCallback* encoded_image_callback_ RTC_GUARDED_BY(local_mutex_) =
        nullptr;
    NiceMock<MockFecControllerOverride> fec_controller_override_;
    std::vector<ResolutionBitrateLimits> resolution_bitrate_limits_
        RTC_GUARDED_BY(local_mutex_);
    int num_set_rates_ RTC_GUARDED_BY(local_mutex_) = 0;
    int num_encodes_ RTC_GUARDED_BY(local_mutex_) = 0;
    std::optional<VideoFrameBuffer::Type> last_input_pixel_format_
        RTC_GUARDED_BY(local_mutex_);
    absl::InlinedVector<VideoFrameBuffer::Type, kMaxPreferredPixelFormats>
        preferred_pixel_formats_ RTC_GUARDED_BY(local_mutex_);
    std::optional<bool> is_qp_trusted_ RTC_GUARDED_BY(local_mutex_);
    std::optional<bool> has_trusted_rate_controller_
        RTC_GUARDED_BY(local_mutex_);
    VideoCodecComplexity last_encoder_complexity_ RTC_GUARDED_BY(local_mutex_){
        VideoCodecComplexity::kComplexityNormal};
  };

  class TestSink : public VideoStreamEncoder::EncoderSink {
   public:
    TestSink(TimeController* time_controller, TestEncoder* test_encoder)
        : time_controller_(time_controller), test_encoder_(test_encoder) {
      RTC_DCHECK(time_controller_);
    }

    void WaitForEncodedFrame(int64_t expected_ntp_time) {
      EXPECT_TRUE(TimedWaitForEncodedFrame(expected_ntp_time, kDefaultTimeout));
    }

    bool TimedWaitForEncodedFrame(int64_t expected_ntp_time,
                                  TimeDelta timeout) {
      uint32_t timestamp = 0;
      if (!WaitForFrame(timeout))
        return false;
      {
        MutexLock lock(&mutex_);
        timestamp = last_timestamp_;
      }
      test_encoder_->CheckLastTimeStampsMatch(expected_ntp_time, timestamp);
      return true;
    }

    void WaitForEncodedFrame(uint32_t expected_width,
                             uint32_t expected_height) {
      EXPECT_TRUE(WaitForFrame(kDefaultTimeout));
      CheckLastFrameSizeMatches(expected_width, expected_height);
    }

    void CheckLastFrameSizeMatches(uint32_t expected_width,
                                   uint32_t expected_height) {
      uint32_t width = 0;
      uint32_t height = 0;
      {
        MutexLock lock(&mutex_);
        width = last_width_;
        height = last_height_;
      }
      EXPECT_EQ(expected_height, height);
      EXPECT_EQ(expected_width, width);
    }

    void CheckLastFrameRotationMatches(VideoRotation expected_rotation) {
      VideoRotation rotation;
      {
        MutexLock lock(&mutex_);
        rotation = last_rotation_;
      }
      EXPECT_EQ(expected_rotation, rotation);
    }

    void ExpectDroppedFrame() {
      EXPECT_FALSE(WaitForFrame(TimeDelta::Millis(100)));
    }

    bool WaitForFrame(TimeDelta timeout) {
      RTC_DCHECK(time_controller_->GetMainThread()->IsCurrent());
      time_controller_->AdvanceTime(TimeDelta::Zero());
      bool ret = encoded_frame_event_.Wait(timeout);
      time_controller_->AdvanceTime(TimeDelta::Zero());
      return ret;
    }

    void SetExpectNoFrames() {
      MutexLock lock(&mutex_);
      expect_frames_ = false;
    }

    int number_of_reconfigurations() const {
      MutexLock lock(&mutex_);
      return number_of_reconfigurations_;
    }

    int last_min_transmit_bitrate() const {
      MutexLock lock(&mutex_);
      return min_transmit_bitrate_bps_;
    }

    void SetNumExpectedLayers(size_t num_layers) {
      MutexLock lock(&mutex_);
      num_expected_layers_ = num_layers;
    }

    int64_t GetLastCaptureTimeMs() const {
      MutexLock lock(&mutex_);
      return last_capture_time_ms_;
    }

    const EncodedImage& GetLastEncodedImage() {
      MutexLock lock(&mutex_);
      return last_encoded_image_;
    }

    std::vector<uint8_t> GetLastEncodedImageData() {
      MutexLock lock(&mutex_);
      return std::move(last_encoded_image_data_);
    }

    VideoLayersAllocation GetLastVideoLayersAllocation() {
      MutexLock lock(&mutex_);
      return last_layers_allocation_;
    }

    int number_of_layers_allocations() const {
      MutexLock lock(&mutex_);
      return number_of_layers_allocations_;
    }

    std::optional<FrameInstrumentationData> GetLastFrameInstrumentationData()
        const {
      MutexLock lock(&mutex_);
      return last_frame_instrumentation_data_;
    }

    void ResetLastFrameInstrumentationData() {
      MutexLock lock(&mutex_);
      last_frame_instrumentation_data_.reset();
    }

    int number_of_dropped_frames() const {
      MutexLock lock(&mutex_);
      return number_of_dropped_frames_;
    }

   private:
    void OnFrameDropped(uint32_t /*rtp_timestamp*/,
                        int /*spatial_id*/,
                        bool /*is_end_of_temporal_unit*/) override {
      MutexLock lock(&mutex_);
      ++number_of_dropped_frames_;
    }

    Result OnEncodedImage(
        const EncodedImage& encoded_image,
        const CodecSpecificInfo* codec_specific_info) override {
      MutexLock lock(&mutex_);
      EXPECT_TRUE(expect_frames_);
      last_encoded_image_ = EncodedImage(encoded_image);
      last_encoded_image_data_ = std::vector<uint8_t>(
          encoded_image.data(), encoded_image.data() + encoded_image.size());
      uint32_t timestamp = encoded_image.RtpTimestamp();
      if (last_timestamp_ != timestamp) {
        num_received_layers_ = 1;
        last_width_ = encoded_image._encodedWidth;
        last_height_ = encoded_image._encodedHeight;
      } else {
        ++num_received_layers_;
        last_width_ = std::max(encoded_image._encodedWidth, last_width_);
        last_height_ = std::max(encoded_image._encodedHeight, last_height_);
      }
      last_timestamp_ = timestamp;
      last_capture_time_ms_ = encoded_image.capture_time_ms_;
      last_rotation_ = encoded_image.rotation_;
      if (num_received_layers_ == num_expected_layers_) {
        encoded_frame_event_.Set();
      }
      if (codec_specific_info &&
          codec_specific_info->frame_instrumentation_data.has_value()) {
        last_frame_instrumentation_data_ =
            codec_specific_info->frame_instrumentation_data;
      }

      return Result(Result::OK, last_timestamp_);
    }

    void OnEncoderConfigurationChanged(
        std::vector<VideoStream> streams,
        bool is_svc,
        VideoEncoderConfig::ContentType content_type,
        int min_transmit_bitrate_bps) override {
      MutexLock lock(&mutex_);
      ++number_of_reconfigurations_;
      min_transmit_bitrate_bps_ = min_transmit_bitrate_bps;
    }

    void OnVideoLayersAllocationUpdated(
        VideoLayersAllocation allocation) override {
      MutexLock lock(&mutex_);
      ++number_of_layers_allocations_;
      last_layers_allocation_ = allocation;
      StringBuilder log;
      for (const auto& layer : allocation.active_spatial_layers) {
        log << layer.width << "x" << layer.height << "@" << layer.frame_rate_fps
            << "[";
        for (const auto target_bitrate :
             layer.target_bitrate_per_temporal_layer) {
          log << target_bitrate.kbps() << ",";
        }
        log << "]";
      }
      RTC_DLOG(LS_INFO) << "OnVideoLayersAllocationUpdated " << log.str();
    }

    TimeController* const time_controller_;
    mutable Mutex mutex_;
    TestEncoder* test_encoder_;
    Event encoded_frame_event_;
    EncodedImage last_encoded_image_;
    std::vector<uint8_t> last_encoded_image_data_;
    uint32_t last_timestamp_ = 0;
    int64_t last_capture_time_ms_ = 0;
    uint32_t last_height_ = 0;
    uint32_t last_width_ = 0;
    VideoRotation last_rotation_ = kVideoRotation_0;
    size_t num_expected_layers_ = 1;
    size_t num_received_layers_ = 0;
    bool expect_frames_ = true;
    int number_of_reconfigurations_ = 0;
    int min_transmit_bitrate_bps_ = 0;
    VideoLayersAllocation last_layers_allocation_ RTC_GUARDED_BY(&mutex_);
    int number_of_layers_allocations_ RTC_GUARDED_BY(mutex_) = 0;
    int number_of_dropped_frames_ RTC_GUARDED_BY(mutex_) = 0;
    std::optional<FrameInstrumentationData> last_frame_instrumentation_data_
        RTC_GUARDED_BY(mutex_);
  };

  class VideoBitrateAllocatorProxyFactory
      : public VideoBitrateAllocatorFactory {
   public:
    VideoBitrateAllocatorProxyFactory()
        : bitrate_allocator_factory_(
              CreateBuiltinVideoBitrateAllocatorFactory()) {}

    std::unique_ptr<VideoBitrateAllocator> Create(
        const Environment& env,
        const VideoCodec& codec) override {
      MutexLock lock(&mutex_);
      codec_config_ = codec;
      return bitrate_allocator_factory_->Create(env, codec);
    }

    VideoCodec codec_config() const {
      MutexLock lock(&mutex_);
      return codec_config_;
    }

   private:
    std::unique_ptr<VideoBitrateAllocatorFactory> bitrate_allocator_factory_;

    mutable Mutex mutex_;
    VideoCodec codec_config_ RTC_GUARDED_BY(mutex_);
  };

  Clock* clock() { return time_controller_.GetClock(); }
  void AdvanceTime(TimeDelta duration) {
    time_controller_.AdvanceTime(duration);
  }

  int64_t CurrentTimeMs() { return clock()->CurrentTime().ms(); }

 protected:
  FieldTrials field_trials_ = CreateTestFieldTrials();
  GlobalSimulatedTimeController time_controller_{Timestamp::Micros(1234)};
  Environment env_ = CreateTestEnvironment(
      {.field_trials = &field_trials_, .time = &time_controller_});
  VideoSendStream::Config video_send_config_;
  VideoEncoderConfig video_encoder_config_;
  int codec_width_;
  int codec_height_;
  int max_framerate_;
  TestEncoder fake_encoder_;
  test::VideoEncoderProxyFactory encoder_factory_;
  VideoBitrateAllocatorProxyFactory bitrate_allocator_factory_;
  std::unique_ptr<MockableSendStatisticsProxy> stats_proxy_;
  TestSink sink_;
  AdaptingFrameForwarder video_source_{&time_controller_};
  // Declared before `video_stream_encoder_` so that it outlives the encoder it
  // is registered with.
  FakeAdaptationConstraint fake_adaptation_constraint_{
      "FakeAdaptationConstraint"};
  std::unique_ptr<VideoStreamEncoderUnderTest> video_stream_encoder_;
  EncoderSwitchRequestCallback encoder_switch_request_callback_;
};

TEST_F(VideoStreamEncoderTest, EncodeOneFrame) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFrame(1, &frame_destroyed_event));
  WaitForEncodedFrame(1);
  EXPECT_TRUE(frame_destroyed_event.Wait(kDefaultTimeout));
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SimulcastIndexNotOverriddenBySpatialIndexZero) {
  NiceMock<MockVideoEncoder> video_encoder;
  test::VideoEncoderProxyFactory encoder_factory(&video_encoder);
  video_send_config_.encoder_settings.encoder_factory = &encoder_factory;

  ON_CALL(video_encoder, GetEncoderInfo)
      .WillByDefault(testing::Return(VideoEncoder::EncoderInfo()));

  EncodedImageCallback* callback = nullptr;
  ON_CALL(video_encoder, RegisterEncodeCompleteCallback)
      .WillByDefault([&](EncodedImageCallback* cb) {
        callback = cb;
        return WEBRTC_VIDEO_CODEC_OK;
      });

  ON_CALL(video_encoder, Encode)
      .WillByDefault(
          [&](const VideoFrame& frame, const std::vector<VideoFrameType>*) {
            EXPECT_TRUE(callback != nullptr);
            if (callback) {
              // Send simulcast stream 0
              {
                EncodedImage encoded_image;
                encoded_image.SetRtpTimestamp(frame.rtp_timestamp());
                // Explicitly set both simulcast and spatial index.
                encoded_image.SetSimulcastIndex(0);
                encoded_image.SetSpatialIndex(0);
                auto buffer = EncodedImageBuffer::Create(100);
                memset(buffer->data(), 0, 100);
                encoded_image.SetEncodedData(buffer);
                CodecSpecificInfo codec_specific;
                codec_specific.codecType = kVideoCodecVP8;
                callback->OnEncodedImage(encoded_image, &codec_specific);
              }
              // Send simulcast stream 1
              {
                EncodedImage encoded_image;
                encoded_image.SetRtpTimestamp(frame.rtp_timestamp());
                // Explicitly set both simulcast and spatial index.
                encoded_image.SetSimulcastIndex(1);
                encoded_image.SetSpatialIndex(0);
                auto buffer = EncodedImageBuffer::Create(100);
                memset(buffer->data(), 0, 100);
                encoded_image.SetEncodedData(buffer);
                CodecSpecificInfo codec_specific;
                codec_specific.codecType = kVideoCodecVP8;
                callback->OnEncodedImage(encoded_image, &codec_specific);
              }
            }
            return WEBRTC_VIDEO_CODEC_OK;
          });

  // Configure the encoder for 2-stream simulcast.
  ResetEncoder("VP8", /*num_streams=*/2, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1, /*screenshare=*/false);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);

  // Since we have 2 streams, the sink expects to receive frames for both
  // streams.
  sink_.SetNumExpectedLayers(2);

  // Incoming frame starts encoding, which creates frame metadata in
  // FrameEncodeMetadataWriter for BOTH streams (index 0 and 1).
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));

  // Wait for the encoded frame. This waits until both layers (streams) are
  // received.
  EXPECT_TRUE(WaitForFrame(kDefaultTimeout));

  // Verifies that the last image received (which is for stream 1) has valid
  // timing flags.
  const EncodedImage& last_image = sink_.GetLastEncodedImage();
  EXPECT_EQ(last_image.SimulcastIndex().value_or(0), 1);
  EXPECT_NE(last_image.timing_.flags, VideoSendTiming::kInvalid);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, PopulatesFrameInstrumentationDataWhenSetTo) {
  video_send_config_.encoder_settings.enable_frame_instrumentation_generator =
      true;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // We need a QP for the encoded frame.
  fake_encoder_.SetEncodedImageData(EncodedImageBuffer::Create(
      kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  WaitForEncodedFrame(1);

  EXPECT_TRUE(sink_.GetLastFrameInstrumentationData().has_value());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       FrameInstrumentationGeneratorDoesNotStashDroppedFrames) {
  // Set low rate but high resolution. Make sure input frame is dropped and
  // instance is released, even with corruption detection enabled.
  const DataRate kLowRate = DataRate::KilobitsPerSec(300);
  codec_width_ = 1280;
  codec_height_ = 720;

  video_send_config_.encoder_settings.enable_frame_instrumentation_generator =
      true;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kLowRate, kLowRate, 0, 0, 0);

  Event frame_destroyed_event;
  // Insert two frames, so that the first one isn't stored in the encoder queue.
  video_source_.IncomingCapturedFrame(CreateFrame(1, &frame_destroyed_event));
  video_source_.IncomingCapturedFrame(CreateFrame(34, /*event=*/nullptr));
  EXPECT_TRUE(frame_destroyed_event.Wait(kDefaultTimeout));

  EXPECT_FALSE(sink_.GetLastFrameInstrumentationData().has_value());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       FrameInstrumentationGeneratorFrameReleasedOnFrameDropped) {
  NiceMock<MockVideoEncoder> video_encoder;
  test::VideoEncoderProxyFactory encoder_factory(&video_encoder);
  video_send_config_.encoder_settings.encoder_factory = &encoder_factory;
  video_send_config_.encoder_settings.enable_frame_instrumentation_generator =
      true;

  EncodedImageCallback* callback = nullptr;
  ON_CALL(video_encoder, RegisterEncodeCompleteCallback)
      .WillByDefault([&](EncodedImageCallback* cb) {
        callback = cb;
        return WEBRTC_VIDEO_CODEC_OK;
      });

  ON_CALL(video_encoder, Encode)
      .WillByDefault(
          [&](const VideoFrame& frame, const std::vector<VideoFrameType>*) {
            EXPECT_TRUE(callback != nullptr);
            if (callback) {
              callback->OnFrameDropped(frame.rtp_timestamp(), /*spatial_id=*/0,
                                       /*is_end_of_temporal_unit=*/true);
            }
            return WEBRTC_VIDEO_CODEC_OK;
          });

  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFrame(1, &frame_destroyed_event));

  // Wait for the encoder queue to process the drop.
  AdvanceTime(TimeDelta::Millis(1));

  // Verify that the frame was released (destruction event triggered).
  EXPECT_TRUE(frame_destroyed_event.Wait(kDefaultTimeout));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       FrameInstrumentationGeneratorHandlesQueuedFrames) {
  video_send_config_.encoder_settings.enable_frame_instrumentation_generator =
      true;
  ConfigureEncoder(video_encoder_config_.Copy());

  // Mark stream as suspended.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::Zero(), DataRate::Zero(), 0, 0, 0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // We need a QP for the encoded frame.
  fake_encoder_.SetEncodedImageData(EncodedImageBuffer::Create(
      kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));

  // Insert a frame, that should be treated as dropped due to suspended state.
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));

  ExpectDroppedFrame();

  // Resume and increase bitrate budget, process stashed frames.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  WaitForEncodedFrame(1);
  EXPECT_TRUE(sink_.GetLastFrameInstrumentationData().has_value());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       DoesNotPopulateFrameInstrumentationDataWhenSetNotTo) {
  video_send_config_.encoder_settings.enable_frame_instrumentation_generator =
      false;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // We need a QP for the encoded frame.
  fake_encoder_.SetEncodedImageData(EncodedImageBuffer::Create(
      kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  WaitForEncodedFrame(1);

  EXPECT_FALSE(sink_.GetLastFrameInstrumentationData().has_value());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       FrameInstrumentationGeneratorNotResetOnConfigurationUnlessEncoderIsToo) {
  // Enable frame instrumentation generator and produce the first keyframe.
  video_send_config_.encoder_settings.enable_frame_instrumentation_generator =
      true;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // We need a QP for the encoded frame.
  fake_encoder_.SetEncodedImageData(EncodedImageBuffer::Create(
      kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  WaitForEncodedFrame(1);

  EXPECT_TRUE(sink_.GetLastFrameInstrumentationData().has_value());
  sink_.ResetLastFrameInstrumentationData();

  // Apply the same configuration again. Encoder should not be reinitilized.
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength, nullptr);

  // Insert delta frames until a frame instrumentation should definitely have
  // been sent.
  for (int i = 1; i < 40; ++i) {
    int timestamp = 1 + (33 * i);
    fake_encoder_.SetEncodedImageData(EncodedImageBuffer::Create(
        kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp, codec_width_, codec_height_));
    WaitForEncodedFrame(timestamp);
  }

  EXPECT_TRUE(sink_.GetLastFrameInstrumentationData().has_value());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropsFramesBeforeFirstOnBitrateUpdated) {
  // Dropped since no target bitrate has been set.
  Event frame_destroyed_event;
  // The encoder will cache up to one frame for a short duration. Adding two
  // frames means that the first frame will be dropped and the second frame will
  // be sent when the encoder is enabled.
  const int64_t kFrame1TimestampMs = CurrentTimeMs();
  video_source_.IncomingCapturedFrame(
      CreateFrame(kFrame1TimestampMs, &frame_destroyed_event));
  AdvanceTime(TimeDelta::Millis(10));
  const int64_t kFrame2TimestampMs = CurrentTimeMs();
  video_source_.IncomingCapturedFrame(CreateFrame(kFrame2TimestampMs, nullptr));
  AdvanceTime(TimeDelta::Zero());
  EXPECT_TRUE(frame_destroyed_event.Wait(kDefaultTimeout));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // The pending frame should be encoded.
  WaitForEncodedFrame(kFrame2TimestampMs);

  const int64_t kFrame3TimestampMs = CurrentTimeMs();
  video_source_.IncomingCapturedFrame(CreateFrame(kFrame3TimestampMs, nullptr));

  WaitForEncodedFrame(kFrame3TimestampMs);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropsFramesWhenRateSetToZero) {
  int64_t time_ms = 123;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(time_ms, nullptr));
  WaitForEncodedFrame(time_ms);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::Zero(), DataRate::Zero(), 0, 0, 0);

  // The encoder will cache up to one frame for a short duration. Adding two
  // frames means that the first frame will be dropped and the second frame will
  // be sent when the encoder is resumed.
  time_ms += 30;
  video_source_.IncomingCapturedFrame(CreateFrame(time_ms, nullptr));
  time_ms += 30;
  video_source_.IncomingCapturedFrame(CreateFrame(time_ms, nullptr));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  WaitForEncodedFrame(time_ms);
  time_ms += 20;
  video_source_.IncomingCapturedFrame(CreateFrame(time_ms, nullptr));
  WaitForEncodedFrame(time_ms);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropsFramesWithSameOrOldNtpTimestamp) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);

  // This frame will be dropped since it has the same ntp timestamp.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));

  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropsFrameAfterStop) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);

  video_stream_encoder_->Stop();
  sink_.SetExpectNoFrames();
  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFrame(2, &frame_destroyed_event));
  EXPECT_TRUE(frame_destroyed_event.Wait(kDefaultTimeout));
}

TEST_F(VideoStreamEncoderTest, DropsPendingFramesOnSlowEncode) {
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  int dropped_count = 0;
  stats_proxy_->SetDroppedFrameCallback(
      [&dropped_count](VideoStreamEncoderObserver::DropReason) {
        ++dropped_count;
      });

  source.IncomingCapturedFrame(CreateFrame(1, nullptr));
  source.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_EQ(1, dropped_count);

  // Two frames arriving within the same millisecond: the first is dropped due
  // to queue overload and must not prevent the second from being encoded.
  source.IncomingCapturedFrame(CreateFrame(3, nullptr));
  source.IncomingCapturedFrame(CreateFrame(3, nullptr));
  WaitForEncodedFrame(3);
  video_stream_encoder_->Stop();
  EXPECT_EQ(2, dropped_count);
}

TEST_F(VideoStreamEncoderTest, DropsFramesOnBitrateOvershoot) {
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Set encoder rate control as not trusted - so that the external frame
  // dropper is activated.
  fake_encoder_.SetIsRateControlTrusted(false);

  DataSize ideal_size_per_frame =
      kTargetBitrate / Frequency::Hertz(kDefaultFramerate);
  // Create an image buffer that is consistently four times larger than
  // expected.
  scoped_refptr<EncodedImageBuffer> encoded_image_buffer =
      EncodedImageBuffer::Create(ideal_size_per_frame.bytes() * 4);
  std::fill(encoded_image_buffer->data(),
            encoded_image_buffer->data() + encoded_image_buffer->size(), 0);

  bool has_media_opt_frame_drop = false;
  stats_proxy_->SetDroppedFrameCallback(
      [&has_media_opt_frame_drop](
          VideoStreamEncoderObserver::DropReason reason) {
        if (reason ==
            VideoStreamEncoderObserver::DropReason::kMediaOptimization) {
          has_media_opt_frame_drop = true;
        }
      });

  for (int i = 0; i < 30; ++i) {
    int64_t timestamp = CurrentTimeMs();
    fake_encoder_.SetEncodedImageData(encoded_image_buffer);
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp, codec_width_, codec_height_));
    AdvanceTime(TimeDelta::Millis(kFrameIntervalMs));
    if (has_media_opt_frame_drop) {
      break;
    }
  }

  EXPECT_TRUE(has_media_opt_frame_drop);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, QualityScalerTriggersOnFrameDropping) {
  // Set the quality scaler checker frequency high to prevent flakiness.
  FieldTrials trials = SetFieldTrial("WebRTC-Video-QualityScalerSettings",
                                     "sampling_period_ms:250");

  // If the quality scaler isn't configured to treat encoder frame drops
  // as a signal for scaling, then this test doesn't make sense to run.
  std::optional<QualityScalingExperiment::Config> config =
      QualityScalingExperiment::GetConfig(trials);
  if (!config.has_value() || !config->use_all_drop_reasons) {
    video_stream_encoder_->Stop();
    GTEST_SKIP() << "QualityScalerExperiment does not take encoder drop "
                    "reasons into account, skipping "
                    "QualityScalerTriggersOnFrameDropping.";
  }

  // Set resolution high so there is a lower resolution to adapt to.
  codec_width_ = 1280;
  codec_height_ = 720;
  ConfigureEncoder(
      video_encoder_config_.Copy(),
      VideoStreamEncoder::BitrateAllocationCallbackType::kVideoLayersAllocation,
      /* num_cores= */ 1, &trials);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Set a low QP so that the qualitu scaler won't trigger because of QP.
  fake_encoder_.SetQp(kQpLow);
  fake_encoder_.SetQualityScaling(true);
  video_source_.set_adaptation_enabled(true);

  // Drop all frames so that the quality scaler will trigger (at the time of
  // writing frame drop percentage > 65% is used in the quality scaler
  // implementation).
  fake_encoder_.ForceDropFrames(true);

  for (int i = 0; i < 100; ++i) {
    int64_t timestamp = CurrentTimeMs();
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp, codec_width_, codec_height_));
    AdvanceTime(TimeDelta::Millis(kFrameIntervalMs));
    // After enough dropped frames the quality scaler should trigger.
    if (stats_proxy_->GetStats().bw_limited_resolution) {
      break;
    }
  }

  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NativeFrameWithoutI420SupportGetsDelivered) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(
      CreateFakeNativeFrame(1, &frame_destroyed_event));
  WaitForEncodedFrame(1);
  EXPECT_EQ(VideoFrameBuffer::Type::kNative,
            fake_encoder_.GetLastInputPixelFormat());
  EXPECT_EQ(fake_encoder_.config().width, fake_encoder_.GetLastInputWidth());
  EXPECT_EQ(fake_encoder_.config().height, fake_encoder_.GetLastInputHeight());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       NativeFrameWithoutI420SupportGetsCroppedIfNecessary) {
  // Use the cropping factory.
  video_encoder_config_.video_stream_factory =
      make_ref_counted<CroppingVideoStreamFactory>();
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config_),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Capture a frame at codec_width_/codec_height_.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  // The encoder will have been configured once.
  EXPECT_EQ(1, sink_.number_of_reconfigurations());
  EXPECT_EQ(codec_width_, fake_encoder_.config().width);
  EXPECT_EQ(codec_height_, fake_encoder_.config().height);

  // Now send in a fake frame that needs to be cropped as the width/height
  // aren't divisible by 4 (see CreateEncoderStreams above).
  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFakeNativeFrame(
      2, &frame_destroyed_event, codec_width_ + 1, codec_height_ + 1));
  WaitForEncodedFrame(2);
  EXPECT_EQ(VideoFrameBuffer::Type::kNative,
            fake_encoder_.GetLastInputPixelFormat());
  EXPECT_EQ(fake_encoder_.config().width, fake_encoder_.GetLastInputWidth());
  EXPECT_EQ(fake_encoder_.config().height, fake_encoder_.GetLastInputHeight());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NonI420FramesShouldNotBeConvertedToI420) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateNV12Frame(1, codec_width_, codec_height_));
  WaitForEncodedFrame(1);
  EXPECT_EQ(VideoFrameBuffer::Type::kNV12,
            fake_encoder_.GetLastInputPixelFormat());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NativeFrameGetsDelivered_NoFrameTypePreference) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  fake_encoder_.SetPreferredPixelFormats({});

  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFakeNV12NativeFrame(
      1, &frame_destroyed_event, codec_width_, codec_height_));
  WaitForEncodedFrame(1);
  EXPECT_EQ(VideoFrameBuffer::Type::kNative,
            fake_encoder_.GetLastInputPixelFormat());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       NativeFrameGetsDelivered_PixelFormatPreferenceMatches) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  fake_encoder_.SetPreferredPixelFormats({VideoFrameBuffer::Type::kNV12});

  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFakeNV12NativeFrame(
      1, &frame_destroyed_event, codec_width_, codec_height_));
  WaitForEncodedFrame(1);
  EXPECT_EQ(VideoFrameBuffer::Type::kNative,
            fake_encoder_.GetLastInputPixelFormat());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NativeFrameGetsDelivered_MappingIsNotFeasible) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Fake NV12 native frame does not allow mapping to I444.
  fake_encoder_.SetPreferredPixelFormats({VideoFrameBuffer::Type::kI444});

  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFakeNV12NativeFrame(
      1, &frame_destroyed_event, codec_width_, codec_height_));
  WaitForEncodedFrame(1);
  EXPECT_EQ(VideoFrameBuffer::Type::kNative,
            fake_encoder_.GetLastInputPixelFormat());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NativeFrameGetsDelivered_BackedByNV12) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFakeNV12NativeFrame(
      1, &frame_destroyed_event, codec_width_, codec_height_));
  WaitForEncodedFrame(1);
  EXPECT_EQ(VideoFrameBuffer::Type::kNative,
            fake_encoder_.GetLastInputPixelFormat());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropsFramesWhenCongestionWindowPushbackSet) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0.5);
  // The congestion window pushback is set to 0.5, which will drop 1/2 of
  // frames. Adding two frames means that the first frame will be dropped and
  // the second frame will be sent to the encoder.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  video_source_.IncomingCapturedFrame(CreateFrame(3, nullptr));
  WaitForEncodedFrame(3);
  video_source_.IncomingCapturedFrame(CreateFrame(4, nullptr));
  video_source_.IncomingCapturedFrame(CreateFrame(5, nullptr));
  WaitForEncodedFrame(5);
  EXPECT_EQ(2u, stats_proxy_->GetStats().frames_dropped_by_congestion_window);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ConfigureEncoderTriggersOnEncoderConfigurationChanged) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  EXPECT_EQ(0, sink_.number_of_reconfigurations());

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  // The encoder will have been configured once when the first frame is
  // received.
  EXPECT_EQ(1, sink_.number_of_reconfigurations());

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.min_transmit_bitrate_bps = 9999;
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_EQ(2, sink_.number_of_reconfigurations());
  EXPECT_EQ(9999, sink_.last_min_transmit_bitrate());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, FrameResolutionChangeReconfigureEncoder) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  // The encoder will have been configured once.
  EXPECT_EQ(1, sink_.number_of_reconfigurations());
  EXPECT_EQ(codec_width_, fake_encoder_.config().width);
  EXPECT_EQ(codec_height_, fake_encoder_.config().height);

  codec_width_ *= 2;
  codec_height_ *= 2;
  // Capture a frame with a higher resolution and wait for it to synchronize
  // with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_EQ(codec_width_, fake_encoder_.config().width);
  EXPECT_EQ(codec_height_, fake_encoder_.config().height);
  EXPECT_EQ(2, sink_.number_of_reconfigurations());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderInstanceDestroyedBeforeAnotherInstanceCreated) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  // Changing the max payload data length recreates encoder.
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength / 2);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_EQ(1, encoder_factory_.GetMaxNumberOfSimultaneousEncoderInstances());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderReleasedBeforeDestructionOnReconfiguration) {
  class ReleaseCheckEncoderProxy : public VideoEncoder {
   public:
    ReleaseCheckEncoderProxy(VideoEncoder* encoder, bool& released)
        : encoder_(encoder), released_(released) {}
    ~ReleaseCheckEncoderProxy() override = default;

    int32_t Encode(const VideoFrame& input_image,
                   const std::vector<VideoFrameType>* frame_types) override {
      return encoder_->Encode(input_image, frame_types);
    }

    int32_t InitEncode(const VideoCodec* config,
                       const Settings& settings) override {
      return encoder_->InitEncode(config, settings);
    }

    int32_t RegisterEncodeCompleteCallback(
        EncodedImageCallback* callback) override {
      return encoder_->RegisterEncodeCompleteCallback(callback);
    }

    int32_t Release() override {
      released_ = true;
      return encoder_->Release();
    }

    void SetRates(const RateControlParameters& parameters) override {
      encoder_->SetRates(parameters);
    }

    VideoEncoder::EncoderInfo GetEncoderInfo() const override {
      return encoder_->GetEncoderInfo();
    }

    void SetFecControllerOverride(
        FecControllerOverride* fec_controller_override) override {
      encoder_->SetFecControllerOverride(fec_controller_override);
    }

   private:
    VideoEncoder* const encoder_;
    bool& released_;
  };

  bool released[2] = {false, false};
  int instance_count = 0;

  test::FunctionVideoEncoderFactory factory(
      [this, &instance_count, &released](const Environment& env,
                                         const SdpVideoFormat& format) {
        RTC_CHECK_LT(instance_count, 2);
        bool& r = released[instance_count++];
        return std::make_unique<ReleaseCheckEncoderProxy>(&fake_encoder_, r);
      });

  video_send_config_.encoder_settings.encoder_factory = &factory;
  ConfigureEncoder(video_encoder_config_.Copy());

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);

  EXPECT_EQ(instance_count, 1);
  EXPECT_FALSE(released[0]);

  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();
  // Changing the max payload data length recreates encoder.
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength / 2);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);

  // We expect that two encoders were created.
  EXPECT_EQ(instance_count, 2);
  // The first encoder should have been released before destruction.
  EXPECT_TRUE(released[0]);

  video_stream_encoder_->Stop();

  // The second encoder should also be released after Stop().
  EXPECT_TRUE(released[1]);
}

TEST_F(VideoStreamEncoderTest, BitrateLimitsChangeReconfigureRateAllocator) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = kTargetBitrate.bps();
  video_stream_encoder_->SetStartBitrate(kStartBitrate.bps());
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  // The encoder will have been configured once when the first frame is
  // received.
  EXPECT_EQ(1, sink_.number_of_reconfigurations());
  EXPECT_EQ(kTargetBitrate.bps(),
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);
  EXPECT_EQ(kStartBitrate.bps(),
            bitrate_allocator_factory_.codec_config().startBitrate * 1000);

  test::FillEncoderConfiguration(kVideoCodecVP8, 1,
                                 &video_encoder_config);  //???
  video_encoder_config.max_bitrate_bps = kTargetBitrate.bps() * 2;
  video_stream_encoder_->SetStartBitrate(kStartBitrate.bps() * 2);
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);

  // Capture a frame and wait for it to synchronize with the encoder thread.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_EQ(2, sink_.number_of_reconfigurations());
  // Bitrate limits have changed - rate allocator should be reconfigured,
  // encoder should not be reconfigured.
  EXPECT_EQ(kTargetBitrate.bps() * 2,
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);
  EXPECT_EQ(kStartBitrate.bps() * 2,
            bitrate_allocator_factory_.codec_config().startBitrate * 1000);
  EXPECT_EQ(1, fake_encoder_.GetNumInitializations());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       IntersectionOfEncoderAndAppBitrateLimitsUsedWhenBothProvided) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const uint32_t kMinEncBitrateKbps = 100;
  const uint32_t kMaxEncBitrateKbps = 1000;
  const VideoEncoder::ResolutionBitrateLimits encoder_bitrate_limits(
      /*frame_size_pixels=*/codec_width_ * codec_height_,
      /*min_start_bitrate_bps=*/0,
      /*min_bitrate_bps=*/kMinEncBitrateKbps * 1000,
      /*max_bitrate_bps=*/kMaxEncBitrateKbps * 1000);
  fake_encoder_.SetResolutionBitrateLimits({encoder_bitrate_limits});

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = (kMaxEncBitrateKbps + 1) * 1000;
  video_encoder_config.simulcast_layers[0].min_bitrate_bps =
      (kMinEncBitrateKbps + 1) * 1000;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  // When both encoder and app provide bitrate limits, the intersection of
  // provided sets should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_EQ(kMaxEncBitrateKbps,
            bitrate_allocator_factory_.codec_config().maxBitrate);
  EXPECT_EQ(kMinEncBitrateKbps + 1,
            bitrate_allocator_factory_.codec_config().minBitrate);

  video_encoder_config.max_bitrate_bps = (kMaxEncBitrateKbps - 1) * 1000;
  video_encoder_config.simulcast_layers[0].min_bitrate_bps =
      (kMinEncBitrateKbps - 1) * 1000;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_EQ(kMaxEncBitrateKbps - 1,
            bitrate_allocator_factory_.codec_config().maxBitrate);
  EXPECT_EQ(kMinEncBitrateKbps,
            bitrate_allocator_factory_.codec_config().minBitrate);

  video_stream_encoder_->Stop();
}

// ReconfigureEncoder checks RTC_ENABLE_H265 flag, and we want to test specific
// behavior of H265, when bitrate limits reported from hardware encoders are 0,
// expecting default target bitrate to be used in this case.
#ifdef RTC_ENABLE_H265
TEST_F(VideoStreamEncoderTest,
       ApplyDefaultBitrateLimitsWhenEncoderInfoResolutionBitrateLimitsAreZero) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  const uint32_t kMinEncBitrateKbps = 100;
  const uint32_t kMaxEncBitrateKbps = 1000;
  const VideoEncoder::ResolutionBitrateLimits encoder_bitrate_limits_180p(
      320 * 180,
      /*min_start_bitrate_bps=*/0,
      /*min_bitrate_bps=*/0,
      /*max_bitrate_bps=*/0);
  fake_encoder_.SetResolutionBitrateLimits({encoder_bitrate_limits_180p});

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecH265, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = kMaxEncBitrateKbps * 1000;
  video_encoder_config.simulcast_layers[0].min_bitrate_bps =
      kMinEncBitrateKbps * 1000;
  video_encoder_config.simulcast_layers[0].width = 320;
  video_encoder_config.simulcast_layers[0].height = 180;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_EQ(bitrate_allocator_factory_.codec_config()
                .simulcastStream[0]
                .targetBitrate,
            kDefaultH265Bitrate180p.kbps());
  video_stream_encoder_->Stop();
}
#endif

TEST_F(VideoStreamEncoderTest,
       EncoderAndAppLimitsDontIntersectEncoderLimitsIgnored) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const uint32_t kMinAppBitrateKbps = 100;
  const uint32_t kMaxAppBitrateKbps = 200;
  const uint32_t kMinEncBitrateKbps = kMaxAppBitrateKbps + 1;
  const uint32_t kMaxEncBitrateKbps = kMaxAppBitrateKbps * 2;
  const VideoEncoder::ResolutionBitrateLimits encoder_bitrate_limits(
      /*frame_size_pixels=*/codec_width_ * codec_height_,
      /*min_start_bitrate_bps=*/0,
      /*min_bitrate_bps=*/kMinEncBitrateKbps * 1000,
      /*max_bitrate_bps=*/kMaxEncBitrateKbps * 1000);
  fake_encoder_.SetResolutionBitrateLimits({encoder_bitrate_limits});

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = kMaxAppBitrateKbps * 1000;
  video_encoder_config.simulcast_layers[0].min_bitrate_bps =
      kMinAppBitrateKbps * 1000;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_EQ(kMaxAppBitrateKbps,
            bitrate_allocator_factory_.codec_config().maxBitrate);
  EXPECT_EQ(kMinAppBitrateKbps,
            bitrate_allocator_factory_.codec_config().minBitrate);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderRecommendedMaxAndMinBitratesUsedForGivenResolution) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const VideoEncoder::ResolutionBitrateLimits encoder_bitrate_limits_270p(
      480 * 270, 34 * 1000, 12 * 1000, 1234 * 1000);
  const VideoEncoder::ResolutionBitrateLimits encoder_bitrate_limits_360p(
      640 * 360, 43 * 1000, 21 * 1000, 2345 * 1000);
  fake_encoder_.SetResolutionBitrateLimits(
      {encoder_bitrate_limits_270p, encoder_bitrate_limits_360p});

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 0;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  // 270p. The bitrate limits recommended by encoder for 270p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 480, 270));
  WaitForEncodedFrame(1);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_270p.min_bitrate_bps),
            bitrate_allocator_factory_.codec_config().minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_270p.max_bitrate_bps),
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);

  // 360p. The bitrate limits recommended by encoder for 360p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(2, 640, 360));
  WaitForEncodedFrame(2);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_360p.min_bitrate_bps),
            bitrate_allocator_factory_.codec_config().minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_360p.max_bitrate_bps),
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);

  // Resolution between 270p and 360p. The bitrate limits recommended by
  // encoder for 360p should be used.
  video_source_.IncomingCapturedFrame(
      CreateFrame(3, (640 + 480) / 2, (360 + 270) / 2));
  WaitForEncodedFrame(3);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_360p.min_bitrate_bps),
            bitrate_allocator_factory_.codec_config().minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_360p.max_bitrate_bps),
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);

  // Resolution higher than 360p. The caps recommended by encoder should be
  // ignored.
  video_source_.IncomingCapturedFrame(CreateFrame(4, 960, 540));
  WaitForEncodedFrame(4);
  EXPECT_NE(static_cast<uint32_t>(encoder_bitrate_limits_270p.min_bitrate_bps),
            bitrate_allocator_factory_.codec_config().minBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(encoder_bitrate_limits_270p.max_bitrate_bps),
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(encoder_bitrate_limits_360p.min_bitrate_bps),
            bitrate_allocator_factory_.codec_config().minBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(encoder_bitrate_limits_360p.max_bitrate_bps),
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);

  // Resolution lower than 270p. The max bitrate limit recommended by encoder
  // for 270p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(5, 320, 180));
  WaitForEncodedFrame(5);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_270p.min_bitrate_bps),
            bitrate_allocator_factory_.codec_config().minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(encoder_bitrate_limits_270p.max_bitrate_bps),
            bitrate_allocator_factory_.codec_config().maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderRecommendedMaxBitrateCapsTargetBitrate) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 0;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  // Encode 720p frame to get the default encoder target bitrate.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  const uint32_t kDefaultTargetBitrateFor720pKbps =
      bitrate_allocator_factory_.codec_config()
          .simulcastStream[0]
          .targetBitrate;

  // Set the max recommended encoder bitrate to something lower than the default
  // target bitrate.
  const VideoEncoder::ResolutionBitrateLimits encoder_bitrate_limits(
      1280 * 720, 10 * 1000, 10 * 1000,
      kDefaultTargetBitrateFor720pKbps / 2 * 1000);
  fake_encoder_.SetResolutionBitrateLimits({encoder_bitrate_limits});

  // Change resolution to trigger encoder reinitialization.
  video_source_.IncomingCapturedFrame(CreateFrame(2, 640, 360));
  WaitForEncodedFrame(2);
  video_source_.IncomingCapturedFrame(CreateFrame(3, 1280, 720));
  WaitForEncodedFrame(3);

  // Ensure the target bitrate is capped by the max bitrate.
  EXPECT_EQ(bitrate_allocator_factory_.codec_config().maxBitrate * 1000,
            static_cast<uint32_t>(encoder_bitrate_limits.max_bitrate_bps));
  EXPECT_EQ(bitrate_allocator_factory_.codec_config()
                    .simulcastStream[0]
                    .targetBitrate *
                1000,
            static_cast<uint32_t>(encoder_bitrate_limits.max_bitrate_bps));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderMaxAndMinBitratesUsedForTwoStreamsHighestActive) {
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits270p(
      480 * 270, 34 * 1000, 12 * 1000, 1234 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits360p(
      640 * 360, 43 * 1000, 21 * 1000, 2345 * 1000);
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderLimits270p, kEncoderLimits360p});

  // Two streams, highest stream active.
  VideoEncoderConfig config;
  const int kNumStreams = 2;
  test::FillEncoderConfiguration(kVideoCodecVP8, kNumStreams, &config);
  config.max_bitrate_bps = 0;
  config.simulcast_layers[0].active = false;
  config.simulcast_layers[1].active = true;
  config.video_stream_factory = nullptr;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);

  // The encoder bitrate limits for 270p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 480, 270));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, kNumStreams);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // The encoder bitrate limits for 360p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(2, 640, 360));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // Resolution b/w 270p and 360p. The encoder limits for 360p should be used.
  video_source_.IncomingCapturedFrame(
      CreateFrame(3, (640 + 480) / 2, (360 + 270) / 2));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // Resolution higher than 360p. Encoder limits should be ignored.
  video_source_.IncomingCapturedFrame(CreateFrame(4, 960, 540));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits270p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits270p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits360p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits360p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // Resolution lower than 270p. The encoder limits for 270p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(5, 320, 180));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       DefaultEncoderMaxAndMinBitratesUsedForTwoStreamsHighestActive) {
  // Two streams, highest stream active.
  VideoEncoderConfig config;
  const int kNumStreams = 2;
  test::FillEncoderConfiguration(kVideoCodecVP8, kNumStreams, &config);
  config.max_bitrate_bps = 0;
  config.simulcast_layers[0].active = false;
  config.simulcast_layers[1].active = true;
  config.video_stream_factory = nullptr;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);

  // Default bitrate limits for 270p should be used.
  const std::optional<VideoEncoder::ResolutionBitrateLimits>
      kDefaultLimits270p =
          EncoderInfoSettings::GetDefaultSinglecastBitrateLimitsForResolution(
              kVideoCodecVP8, 480 * 270);
  video_source_.IncomingCapturedFrame(CreateFrame(1, 480, 270));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, kNumStreams);
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits270p->min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits270p->max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // Default bitrate limits for 360p should be used.
  const std::optional<VideoEncoder::ResolutionBitrateLimits>
      kDefaultLimits360p =
          EncoderInfoSettings::GetDefaultSinglecastBitrateLimitsForResolution(
              kVideoCodecVP8, 640 * 360);
  video_source_.IncomingCapturedFrame(CreateFrame(2, 640, 360));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits360p->min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits360p->max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // Resolution b/w 270p and 360p. The default limits for 360p should be used.
  video_source_.IncomingCapturedFrame(
      CreateFrame(3, (640 + 480) / 2, (360 + 270) / 2));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits360p->min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits360p->max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // Default bitrate limits for 540p should be used.
  const std::optional<VideoEncoder::ResolutionBitrateLimits>
      kDefaultLimits540p =
          EncoderInfoSettings::GetDefaultSinglecastBitrateLimitsForResolution(
              kVideoCodecVP8, 960 * 540);
  video_source_.IncomingCapturedFrame(CreateFrame(4, 960, 540));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits540p->min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kDefaultLimits540p->max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderMaxAndMinBitratesUsedForThreeStreamsMiddleActive) {
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits270p(
      480 * 270, 34 * 1000, 12 * 1000, 1234 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits360p(
      640 * 360, 43 * 1000, 21 * 1000, 2345 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits720p(
      1280 * 720, 54 * 1000, 31 * 1000, 3456 * 1000);
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderLimits270p, kEncoderLimits360p, kEncoderLimits720p});

  // Three streams, middle stream active.
  VideoEncoderConfig config;
  const int kNumStreams = 3;
  test::FillEncoderConfiguration(kVideoCodecVP8, kNumStreams, &config);
  config.simulcast_layers[0].active = false;
  config.simulcast_layers[1].active = true;
  config.simulcast_layers[2].active = false;
  config.video_stream_factory = nullptr;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);

  // The encoder bitrate limits for 360p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, kNumStreams);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // The encoder bitrate limits for 270p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(2, 960, 540));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderMaxAndMinBitratesNotUsedForThreeStreamsLowestActive) {
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits270p(
      480 * 270, 34 * 1000, 12 * 1000, 1234 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits360p(
      640 * 360, 43 * 1000, 21 * 1000, 2345 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits720p(
      1280 * 720, 54 * 1000, 31 * 1000, 3456 * 1000);
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderLimits270p, kEncoderLimits360p, kEncoderLimits720p});

  // Three streams, lowest stream active.
  VideoEncoderConfig config;
  const int kNumStreams = 3;
  test::FillEncoderConfiguration(kVideoCodecVP8, kNumStreams, &config);
  config.simulcast_layers[0].active = true;
  config.simulcast_layers[1].active = false;
  config.simulcast_layers[2].active = false;
  config.video_stream_factory = nullptr;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);

  // Resolution on lowest stream lower than 270p. The encoder limits not applied
  // on lowest stream, limits for 270p should not be used
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, kNumStreams);
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits270p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits270p.max_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderMaxBitrateCappedByConfigForTwoStreamsHighestActive) {
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits270p(
      480 * 270, 34 * 1000, 12 * 1000, 1234 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits360p(
      640 * 360, 43 * 1000, 21 * 1000, 2345 * 1000);
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderLimits270p, kEncoderLimits360p});
  const int kMaxBitrateBps = kEncoderLimits360p.max_bitrate_bps - 100 * 1000;

  // Two streams, highest stream active.
  VideoEncoderConfig config;
  const int kNumStreams = 2;
  test::FillEncoderConfiguration(kVideoCodecVP8, kNumStreams, &config);
  config.simulcast_layers[0].active = false;
  config.simulcast_layers[1].active = true;
  config.simulcast_layers[1].max_bitrate_bps = kMaxBitrateBps;
  config.video_stream_factory = nullptr;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);

  // The max configured bitrate should be used.
  // The encoder bitrate limits for 270p should be used for min bitrate
  video_source_.IncomingCapturedFrame(CreateFrame(1, 480, 270));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, kNumStreams);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kMaxBitrateBps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  // The max configured bitrate should be used.
  // The encoder bitrate limits for 360p should be used for min bitrate
  video_source_.IncomingCapturedFrame(CreateFrame(2, 640, 360));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.min_bitrate_bps),
            fake_encoder_.config().simulcastStream[1].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kMaxBitrateBps),
            fake_encoder_.config().simulcastStream[1].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ConfigureEncoderRequestsRefreshFrameOnScaleResolutionDownToChange) {
  MockVideoSourceInterface mock_source;
  video_stream_encoder_->SetSource(&mock_source,
                                   DegradationPreference::DISABLED);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Initial configuration.
  VideoEncoderConfig config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &config);
  config.simulcast_layers[0].scale_resolution_down_to = {
      .width = 640, .height = 360};  // Example resolution
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);

  EXPECT_CALL(mock_source, RequestRefreshFrame).Times(1);

  // Change scale_resolution_down_to.
  config.simulcast_layers[0].scale_resolution_down_to = {
      .width = 320, .height = 180};  // Different resolution
  video_stream_encoder_->ConfigureEncoder(std::move(config), kMaxPayloadLength);

  AdvanceTime(TimeDelta::Millis(0));
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SwitchSourceDeregisterEncoderAsSink) {
  EXPECT_TRUE(video_source_.has_sinks());
  test::FrameForwarder new_video_source;
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_FALSE(video_source_.has_sinks());
  EXPECT_TRUE(new_video_source.has_sinks());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SinkWantsRotationApplied) {
  EXPECT_FALSE(video_source_.sink_wants().rotation_applied);
  video_stream_encoder_->SetSink(&sink_, true /*rotation_applied*/);
  EXPECT_TRUE(video_source_.sink_wants().rotation_applied);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SinkWantsDefaultUnlimitedBeforeFirstFrame) {
  ASSERT_TRUE(video_source_.has_sinks());
  EXPECT_THAT(video_source_.sink_wants(), UnlimitedSinkWants());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderMaxPixelsPerFrameAppliedToSinkWants) {
  constexpr int kMaxPixels = 640 * 360;
  fake_encoder_.SetMaxPixelsPerFrame(kMaxPixels);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // The first frame configures the encoder and pushes its limit to the source.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  EXPECT_EQ(video_source_.sink_wants().max_pixel_count, kMaxPixels);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderMaxPixelsPerFrameDownscalesSource) {
  constexpr int kMaxPixels = 640 * 360;
  fake_encoder_.SetMaxPixelsPerFrame(kMaxPixels);
  video_source_.set_adaptation_enabled(true);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  int64_t timestamp_ms = kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);

  // The next frame is downscaled by the source to fit the encoder's limit.
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_LE(
      fake_encoder_.GetLastInputWidth() * fake_encoder_.GetLastInputHeight(),
      kMaxPixels);
  EXPECT_LT(fake_encoder_.GetLastInputWidth(), 1280);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderWithoutMaxPixelsPerFrameLeavesSinkWantsUnlimited) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  EXPECT_EQ(video_source_.sink_wants().max_pixel_count,
            std::numeric_limits<int>::max());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       FrameRateLimitPropagatedToSinkWantsBeforeFirstFrame) {
  ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
  // Set max wanted frame rate.
  video_encoder_config_.simulcast_layers[0].max_framerate = 15;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);
  EXPECT_EQ(video_source_.sink_wants().max_framerate_fps, 15);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, FrameRateLimitIgnoredOnInactiveStreams) {
  ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
  VideoEncoderConfig encoder_config = video_encoder_config_.Copy();
  encoder_config.simulcast_layers.push_back(encoder_config.simulcast_layers[0]);

  encoder_config.simulcast_layers[0].max_framerate = 15;
  encoder_config.simulcast_layers[1].max_framerate = 30;
  encoder_config.simulcast_layers[1].active = false;

  video_stream_encoder_->ConfigureEncoder(std::move(encoder_config),
                                          kMaxPayloadLength);
  EXPECT_EQ(video_source_.sink_wants().max_framerate_fps, 15);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, FrameRateLimitCanBeReset) {
  ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
  // Set max wanted frame rate.
  video_encoder_config_.simulcast_layers[0].max_framerate = 15;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);
  EXPECT_EQ(video_source_.sink_wants().max_framerate_fps, 15);

  video_encoder_config_.simulcast_layers[0].max_framerate =
      VideoStream().max_framerate;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);
  EXPECT_THAT(video_source_.sink_wants(), FpsUnlimited());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, RequestInSinkWantsBeforeFirstFrame) {
  // Use a real video stream factory or else `scale_resolution_down_to` is not
  // applied correctly.
  video_encoder_config_.video_stream_factory = nullptr;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
  video_encoder_config_.simulcast_layers[0].scale_resolution_down_to.emplace(
      Resolution({.width = 320, .height = 160}));

  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);

  EXPECT_EQ(video_source_.sink_wants().requested_resolution,
            VideoSinkWants::FrameSize(320, 160));

  video_encoder_config_.simulcast_layers[0].scale_resolution_down_to->height =
      320;
  video_encoder_config_.simulcast_layers[0].scale_resolution_down_to->width =
      640;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);

  EXPECT_EQ(video_source_.sink_wants().requested_resolution,
            VideoSinkWants::FrameSize(640, 320));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, RequestInWrongAspectRatioWithAdapter) {
  // Use a real video stream factory or else `scale_resolution_down_to` is not
  // applied correctly.
  video_encoder_config_.video_stream_factory = nullptr;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Use a source that adapts resolution based on OnSinkWants.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
  video_encoder_config_.simulcast_layers[0].scale_resolution_down_to = {
      .width = 30, .height = 30};
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);

  // Capture a 60x30 frame.
  source.IncomingCapturedFrame(CreateFrame(1, 60, 30));
  // The 60x30 frame does not fit inside the 30x30 restrictions.
  // Expect 30x15, maintaining aspect ratio.
  WaitForEncodedFrame(30, 15);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncodingActiveFlagPropagatedToSinkWantsBeforeFirstFrame) {
  ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
  video_encoder_config_.simulcast_layers[0].active = false;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);
  EXPECT_FALSE(video_source_.sink_wants().is_active);

  video_encoder_config_.simulcast_layers[0].active = true;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);
  EXPECT_TRUE(video_source_.sink_wants().is_active);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, CorrectlyAdjustsAv1Bitrate) {
  ResetEncoder("AV1", /*num_streams*/ 2, /*num_temporal_layers=*/2,
               /*num_spatial_layers=*/1, /*screenshare*/ false,
               kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);

  // Let link allocation and stable bitrate be 2x the target bitrate.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
  WaitForEncodedFrame(CurrentTimeMs());

  // Before enough data has been gathered, some default pushback is applied.
  VideoEncoder::RateControlParameters rate_settings =
      *fake_encoder_.GetAndResetLastRateControlSettings();
  // Allow 5% diff from target bitrate.
  const double allowed_error_bps =
      rate_settings.target_bitrate.get_sum_bps() * 0.05;
  EXPECT_NEAR(rate_settings.bitrate.get_sum_bps(),
              rate_settings.target_bitrate.get_sum_bps() /
                  EncoderBitrateAdjuster::kDefaultUtilizationFactor,
              allowed_error_bps);

  // Insert frames until bitrate adjuster is saturated.
  const TimeDelta runtime = EncoderBitrateAdjuster::kWindowSize;
  const Timestamp start_time = clock()->CurrentTime();
  while (clock()->CurrentTime() - start_time < runtime) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
    WaitForEncodedFrame(CurrentTimeMs());
  }

  // Make sure rate has been reallocated.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate - DataRate::BitsPerSec(500), kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
  WaitForEncodedFrame(CurrentTimeMs());

  // Pushback should no longer happen.
  rate_settings = *fake_encoder_.GetAndResetLastRateControlSettings();
  EXPECT_NEAR(rate_settings.bitrate.get_sum_bps(),
              rate_settings.target_bitrate.get_sum_bps(), allowed_error_bps);

  video_stream_encoder_->Stop();
}

class ResolutionAlignmentTest
    : public VideoStreamEncoderTest,
      public ::testing::WithParamInterface<
          ::testing::tuple<int, std::vector<double>>> {
 public:
  ResolutionAlignmentTest()
      : requested_alignment_(::testing::get<0>(GetParam())),
        scale_factors_(::testing::get<1>(GetParam())) {}

 protected:
  const uint32_t requested_alignment_;
  const std::vector<double> scale_factors_;
};

INSTANTIATE_TEST_SUITE_P(
    AlignmentAndScaleFactors,
    ResolutionAlignmentTest,
    ::testing::Combine(
        ::testing::Values(1, 2, 3, 4, 5, 6, 16, 22),  // requested_alignment_
        ::testing::Values(std::vector<double>{-1.0},  // scale_factors_
                          std::vector<double>{-1.0, -1.0},
                          std::vector<double>{-1.0, -1.0, -1.0},
                          std::vector<double>{4.0, 2.0, 1.0},
                          std::vector<double>{9999.0, -1.0, 1.0},
                          std::vector<double>{3.99, 2.01, 1.0},
                          std::vector<double>{4.9, 1.7, 1.25},
                          std::vector<double>{10.0, 4.0, 3.0},
                          std::vector<double>{1.75, 3.5},
                          std::vector<double>{1.5, 2.5},
                          std::vector<double>{1.3, 1.0})));

TEST_P(ResolutionAlignmentTest, SinkWantsAlignmentApplied) {
  // Set requested resolution alignment.
  video_source_.set_adaptation_enabled(true);
  fake_encoder_.SetRequestedResolutionAlignment(requested_alignment_);
  fake_encoder_.SetApplyAlignmentToAllSimulcastLayers(true);

  // Fill config with the scaling factor by which to reduce encoding size.
  const int num_streams = scale_factors_.size();
  VideoEncoderConfig config;
  test::FillEncoderConfiguration(kVideoCodecVP8, num_streams, &config);
  for (int i = 0; i < num_streams; ++i) {
    config.simulcast_layers[i].scale_resolution_down_by = scale_factors_[i];
  }
  config.video_stream_factory = nullptr;
  video_stream_encoder_->ConfigureEncoder(std::move(config), kMaxPayloadLength);

  // We can get up to 3 streams of 1280x720 resolution each in this test. Make
  // available bitrate large enough to get all streams encoded.
  const DataRate kAvailableBitrate = 3 * kSimulcastTargetBitrate;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kAvailableBitrate, kAvailableBitrate, 0, 0, 0);
  // Wait for all layers before triggering event.
  sink_.SetNumExpectedLayers(num_streams);

  // On the 1st frame, we should have initialized the encoder and
  // asked for its resolution requirements.
  int64_t timestamp_ms = kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(1, fake_encoder_.GetNumInitializations());

  // On the 2nd frame, we should be receiving a correctly aligned resolution.
  // (It's up the to the encoder to potentially drop the previous frame,
  // to avoid coding back-to-back keyframes.)
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_GE(fake_encoder_.GetNumInitializations(), 1);

  VideoCodec codec = fake_encoder_.config();
  EXPECT_EQ(codec.numberOfSimulcastStreams, num_streams);
  // Frame size should be a multiple of the requested alignment.
  for (int i = 0; i < codec.numberOfSimulcastStreams; ++i) {
    EXPECT_EQ(codec.simulcastStream[i].width % requested_alignment_, 0u);
    EXPECT_EQ(codec.simulcastStream[i].height % requested_alignment_, 0u);
    // Aspect ratio should match.
    EXPECT_EQ(codec.width * codec.simulcastStream[i].height,
              codec.height * codec.simulcastStream[i].width);
  }

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, TestCpuDowngrades_BalancedMode) {
  const int kFramerateFps = 30;
  const int kWidth = 1280;
  const int kHeight = 720;

  ASSERT_EQ(video_encoder_config_.simulcast_layers.size(), 1u);
  video_encoder_config_.simulcast_layers[0].width = kWidth;
  video_encoder_config_.simulcast_layers[0].height = kHeight;
  video_encoder_config_.simulcast_layers[0].max_framerate = kFramerateFps;
  ConfigureEncoder(video_encoder_config_.Copy());

  // We rely on the automatic resolution adaptation, but we handle framerate
  // adaptation manually by mocking the stats proxy.
  video_source_.set_adaptation_enabled(true);

  // Enable BALANCED preference, no initial limitation.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_stream_encoder_->SetSource(&video_source_,
                                   DegradationPreference::BALANCED);
  EXPECT_THAT(video_source_.sink_wants(), WantsFps(Eq(kFramerateFps)));
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Adapt down as far as possible.
  VideoSinkWants last_wants;
  int64_t t = 1;
  int loop_count = 0;
  do {
    ++loop_count;
    last_wants = video_source_.sink_wants();

    // Simulate the framerate we've been asked to adapt to.
    const int fps = std::min(kFramerateFps, last_wants.max_framerate_fps);
    const int frame_interval_ms = kNumMillisecsPerSec / fps;
    VideoSendStream::Stats mock_stats = stats_proxy_->GetStats();
    mock_stats.input_frame_rate = fps;
    stats_proxy_->SetMockStats(mock_stats);

    video_source_.IncomingCapturedFrame(CreateFrame(t, kWidth, kHeight));
    sink_.WaitForEncodedFrame(t);
    t += frame_interval_ms;

    video_stream_encoder_->TriggerCpuOveruse();
    EXPECT_THAT(
        video_source_.sink_wants(),
        FpsInRangeForPixelsInBalanced(*video_source_.last_sent_width() *
                                      *video_source_.last_sent_height()));
  } while (video_source_.sink_wants().max_pixel_count <
               last_wants.max_pixel_count ||
           video_source_.sink_wants().max_framerate_fps <
               last_wants.max_framerate_fps);

  // Verify that we've adapted all the way down.
  stats_proxy_->ResetMockStats();
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(loop_count - 1,
            stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(kMinPixelsPerFrame, *video_source_.last_sent_width() *
                                    *video_source_.last_sent_height());
  EXPECT_EQ(kMinBalancedFramerateFps,
            video_source_.sink_wants().max_framerate_fps);

  // Adapt back up the same number of times we adapted down.
  for (int i = 0; i < loop_count - 1; ++i) {
    last_wants = video_source_.sink_wants();

    // Simulate the framerate we've been asked to adapt to.
    const int fps = std::min(kFramerateFps, last_wants.max_framerate_fps);
    const int frame_interval_ms = kNumMillisecsPerSec / fps;
    VideoSendStream::Stats mock_stats = stats_proxy_->GetStats();
    mock_stats.input_frame_rate = fps;
    stats_proxy_->SetMockStats(mock_stats);

    video_source_.IncomingCapturedFrame(CreateFrame(t, kWidth, kHeight));
    sink_.WaitForEncodedFrame(t);
    t += frame_interval_ms;

    video_stream_encoder_->TriggerCpuUnderuse();
    EXPECT_THAT(
        video_source_.sink_wants(),
        FpsInRangeForPixelsInBalanced(*video_source_.last_sent_width() *
                                      *video_source_.last_sent_height()));
    EXPECT_TRUE(video_source_.sink_wants().max_pixel_count >
                    last_wants.max_pixel_count ||
                video_source_.sink_wants().max_framerate_fps >
                    last_wants.max_framerate_fps);
  }

  EXPECT_THAT(video_source_.sink_wants(), FpsMaxResolutionMax());
  stats_proxy_->ResetMockStats();
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ((loop_count - 1) * 2,
            stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       SinkWantsNotChangedByResourceLimitedBeforeDegradationPreferenceChange) {
  video_stream_encoder_->OnBitrateUpdated(kTargetBitrate, kTargetBitrate, 0, 0,
                                          0);
  EXPECT_THAT(video_source_.sink_wants(), UnlimitedSinkWants());

  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  int64_t ntp_time = kFrameIntervalMs;

  // Force an input frame rate to be available, or the adaptation call won't
  // know what framerate to adapt form.
  const int kInputFps = 30;
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kInputFps;
  stats_proxy_->SetMockStats(stats);

  video_source_.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&video_source_,
                                   DegradationPreference::MAINTAIN_RESOLUTION);
  EXPECT_THAT(video_source_.sink_wants(), UnlimitedSinkWants());
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += kFrameIntervalMs;

  // Trigger CPU overuse.
  video_stream_encoder_->TriggerCpuOveruse();
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += kFrameIntervalMs;

  EXPECT_FALSE(video_source_.sink_wants().target_pixel_count);
  EXPECT_EQ(std::numeric_limits<int>::max(),
            video_source_.sink_wants().max_pixel_count);
  // Some framerate constraint should be set.
  int restricted_fps = video_source_.sink_wants().max_framerate_fps;
  EXPECT_LT(restricted_fps, kInputFps);
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += 100;

  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &video_source_, DegradationPreference::MAINTAIN_FRAMERATE);
  // Give the encoder queue time to process the change in degradation preference
  // by waiting for an encoded frame.
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += kFrameIntervalMs;

  video_stream_encoder_->TriggerQualityLow();
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += kFrameIntervalMs;

  // Some resolution constraint should be set.
  EXPECT_FALSE(video_source_.sink_wants().target_pixel_count);
  EXPECT_LT(video_source_.sink_wants().max_pixel_count,
            kFrameWidth * kFrameHeight);
  EXPECT_THAT(video_source_.sink_wants(), FpsUnlimited());

  int pixel_count = video_source_.sink_wants().max_pixel_count;
  // Triggering a CPU underuse should not change the sink wants since it has
  // not been overused for resolution since we changed degradation preference.
  video_stream_encoder_->TriggerCpuUnderuse();
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += kFrameIntervalMs;
  EXPECT_EQ(video_source_.sink_wants().max_pixel_count, pixel_count);

  EXPECT_THAT(video_source_.sink_wants(), FpsUnlimited());

  // Change the degradation preference back. CPU underuse should not adapt since
  // QP is most limited.
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &video_source_, DegradationPreference::MAINTAIN_RESOLUTION);
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += 100;
  // Resolution adaptations is gone after changing degradation preference.
  EXPECT_FALSE(video_source_.sink_wants().target_pixel_count);
  EXPECT_EQ(std::numeric_limits<int>::max(),
            video_source_.sink_wants().max_pixel_count);
  // The fps adaptation from above is now back.
  EXPECT_EQ(video_source_.sink_wants().max_framerate_fps, restricted_fps);

  // Trigger CPU underuse.
  video_stream_encoder_->TriggerCpuUnderuse();
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += kFrameIntervalMs;
  EXPECT_EQ(video_source_.sink_wants().max_framerate_fps, restricted_fps);

  // Trigger QP underuse, fps should return to normal.
  video_stream_encoder_->TriggerQualityHigh();
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time, kFrameWidth, kFrameHeight));
  sink_.WaitForEncodedFrame(ntp_time);
  ntp_time += kFrameIntervalMs;
  EXPECT_THAT(video_source_.sink_wants(), UnlimitedSinkWants());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SinkWantsStoredByDegradationPreference) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  EXPECT_THAT(video_source_.sink_wants(), UnlimitedSinkWants());

  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  int64_t frame_timestamp = 1;

  video_source_.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;

  // Trigger CPU overuse.
  video_stream_encoder_->TriggerCpuOveruse();
  video_source_.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;

  // Default degradation preference is maintain-framerate, so will lower max
  // wanted resolution.
  EXPECT_FALSE(video_source_.sink_wants().target_pixel_count);
  EXPECT_LT(video_source_.sink_wants().max_pixel_count,
            kFrameWidth * kFrameHeight);
  EXPECT_THAT(video_source_.sink_wants(), FpsUnlimited());

  // Set new source, switch to maintain-resolution.
  test::FrameForwarder new_video_source;
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &new_video_source, DegradationPreference::MAINTAIN_RESOLUTION);
  // Give the encoder queue time to process the change in degradation preference
  // by waiting for an encoded frame.
  new_video_source.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameWidth));
  sink_.WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;
  // Initially no degradation registered.
  EXPECT_THAT(new_video_source.sink_wants(), UnlimitedSinkWants());

  // Force an input frame rate to be available, or the adaptation call won't
  // know what framerate to adapt form.
  const int kInputFps = 30;
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kInputFps;
  stats_proxy_->SetMockStats(stats);

  video_stream_encoder_->TriggerCpuOveruse();
  new_video_source.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;

  // Some framerate constraint should be set.
  EXPECT_FALSE(new_video_source.sink_wants().target_pixel_count);
  EXPECT_EQ(std::numeric_limits<int>::max(),
            new_video_source.sink_wants().max_pixel_count);
  EXPECT_LT(new_video_source.sink_wants().max_framerate_fps, kInputFps);

  // Turn off degradation completely.
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &new_video_source,
      DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION);
  // Give the encoder queue time to process the change in degradation preference
  // by waiting for an encoded frame.
  new_video_source.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameWidth));
  sink_.WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;
  EXPECT_THAT(new_video_source.sink_wants(), UnlimitedSinkWants());

  video_stream_encoder_->TriggerCpuOveruse();
  new_video_source.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;

  // Still no degradation.
  EXPECT_THAT(new_video_source.sink_wants(), UnlimitedSinkWants());

  // Calling SetSource with resolution scaling enabled apply the old SinkWants.
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &new_video_source, DegradationPreference::MAINTAIN_FRAMERATE);
  // Give the encoder queue time to process the change in degradation preference
  // by waiting for an encoded frame.
  new_video_source.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameWidth));
  sink_.WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;
  EXPECT_LT(new_video_source.sink_wants().max_pixel_count,
            kFrameWidth * kFrameHeight);
  EXPECT_FALSE(new_video_source.sink_wants().target_pixel_count);
  EXPECT_THAT(new_video_source.sink_wants(), FpsUnlimited());

  // Calling SetSource with framerate scaling enabled apply the old SinkWants.
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &new_video_source, DegradationPreference::MAINTAIN_RESOLUTION);
  // Give the encoder queue time to process the change in degradation preference
  // by waiting for an encoded frame.
  new_video_source.IncomingCapturedFrame(
      CreateFrame(frame_timestamp, kFrameWidth, kFrameWidth));
  sink_.WaitForEncodedFrame(frame_timestamp);
  frame_timestamp += kFrameIntervalMs;
  EXPECT_FALSE(new_video_source.sink_wants().target_pixel_count);
  EXPECT_EQ(std::numeric_limits<int>::max(),
            new_video_source.sink_wants().max_pixel_count);
  EXPECT_LT(new_video_source.sink_wants().max_framerate_fps, kInputFps);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, StatsTracksQualityAdaptationStats) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kWidth = 1280;
  const int kHeight = 720;
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(1);
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_EQ(0, stats.number_of_quality_adapt_changes);

  // Trigger adapt down.
  video_stream_encoder_->TriggerQualityLow();
  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  WaitForEncodedFrame(2);

  stats = stats_proxy_->GetStats();
  EXPECT_TRUE(stats.bw_limited_resolution);
  EXPECT_EQ(1, stats.number_of_quality_adapt_changes);

  // Trigger adapt up.
  video_stream_encoder_->TriggerQualityHigh();
  video_source_.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  WaitForEncodedFrame(3);

  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_EQ(2, stats.number_of_quality_adapt_changes);
  EXPECT_EQ(0, stats.number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, StatsTracksCpuAdaptationStats) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kWidth = 1280;
  const int kHeight = 720;
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(1);
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_EQ(0, stats.number_of_cpu_adapt_changes);

  // Trigger CPU overuse.
  video_stream_encoder_->TriggerCpuOveruse();
  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  WaitForEncodedFrame(2);

  stats = stats_proxy_->GetStats();
  EXPECT_TRUE(stats.cpu_limited_resolution);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Trigger CPU normal use.
  video_stream_encoder_->TriggerCpuUnderuse();
  video_source_.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  WaitForEncodedFrame(3);

  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats.number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SwitchingSourceKeepsCpuAdaptation) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kWidth = 1280;
  const int kHeight = 720;
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(1);
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_EQ(0, stats.number_of_cpu_adapt_changes);

  // Trigger CPU overuse.
  video_stream_encoder_->TriggerCpuOveruse();
  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  WaitForEncodedFrame(2);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_TRUE(stats.cpu_limited_resolution);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Set new source with adaptation still enabled.
  test::FrameForwarder new_video_source;
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  new_video_source.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  WaitForEncodedFrame(3);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_TRUE(stats.cpu_limited_resolution);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Set adaptation disabled.
  video_stream_encoder_->SetSource(
      &new_video_source,
      DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION);

  new_video_source.IncomingCapturedFrame(CreateFrame(4, kWidth, kHeight));
  WaitForEncodedFrame(4);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Set adaptation back to enabled.
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  new_video_source.IncomingCapturedFrame(CreateFrame(5, kWidth, kHeight));
  WaitForEncodedFrame(5);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_TRUE(stats.cpu_limited_resolution);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Trigger CPU normal use.
  video_stream_encoder_->TriggerCpuUnderuse();
  new_video_source.IncomingCapturedFrame(CreateFrame(6, kWidth, kHeight));
  WaitForEncodedFrame(6);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats.number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SwitchingSourceKeepsQualityAdaptation) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kWidth = 1280;
  const int kHeight = 720;
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(1);
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.bw_limited_framerate);
  EXPECT_EQ(0, stats.number_of_quality_adapt_changes);

  // Set new source with adaptation still enabled.
  test::FrameForwarder new_video_source;
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::BALANCED);

  new_video_source.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  WaitForEncodedFrame(2);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.bw_limited_framerate);
  EXPECT_EQ(0, stats.number_of_quality_adapt_changes);

  // Trigger adapt down.
  video_stream_encoder_->TriggerQualityLow();
  new_video_source.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  WaitForEncodedFrame(3);
  stats = stats_proxy_->GetStats();
  EXPECT_TRUE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.bw_limited_framerate);
  EXPECT_EQ(1, stats.number_of_quality_adapt_changes);

  // Set new source with adaptation still enabled.
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::BALANCED);

  new_video_source.IncomingCapturedFrame(CreateFrame(4, kWidth, kHeight));
  WaitForEncodedFrame(4);
  stats = stats_proxy_->GetStats();
  EXPECT_TRUE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.bw_limited_framerate);
  EXPECT_EQ(1, stats.number_of_quality_adapt_changes);

  // Disable resolution scaling.
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);

  new_video_source.IncomingCapturedFrame(CreateFrame(5, kWidth, kHeight));
  WaitForEncodedFrame(5);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.bw_limited_resolution);
  EXPECT_FALSE(stats.bw_limited_framerate);
  EXPECT_EQ(1, stats.number_of_quality_adapt_changes);
  EXPECT_EQ(0, stats.number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       QualityAdaptationStatsAreResetWhenScalerIsDisabled) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kWidth = 1280;
  const int kHeight = 720;
  int64_t timestamp_ms = kFrameIntervalMs;
  video_source_.set_adaptation_enabled(true);
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt down.
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger overuse.
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Leave source unchanged, but disable quality scaler.
  fake_encoder_.SetQualityScaling(false);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  // Make format different, to force recreation of encoder.
  video_encoder_config.video_format.parameters["foo"] = "foo";
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       StatsTracksCpuAdaptationStatsWhenSwitchingSource_Balanced) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kWidth = 1280;
  const int kHeight = 720;
  int sequence = 1;

  // Enable BALANCED preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);
  source.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(0, stats.number_of_cpu_adapt_changes);

  // Trigger CPU overuse, should now adapt down.
  video_stream_encoder_->TriggerCpuOveruse();
  source.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Set new degradation preference should clear restrictions since we changed
  // from BALANCED.
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &source, DegradationPreference::MAINTAIN_FRAMERATE);
  source.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Force an input frame rate to be available, or the adaptation call won't
  // know what framerate to adapt from.
  VideoSendStream::Stats mock_stats = stats_proxy_->GetStats();
  mock_stats.input_frame_rate = 30;
  stats_proxy_->SetMockStats(mock_stats);
  video_stream_encoder_->TriggerCpuOveruse();
  stats_proxy_->ResetMockStats();
  source.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);

  // We have now adapted once.
  stats = stats_proxy_->GetStats();
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);

  // Back to BALANCED, should clear the restrictions again.
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &source, DegradationPreference::BALANCED);
  source.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       StatsTracksCpuAdaptationStatsWhenSwitchingSource) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kWidth = 1280;
  const int kHeight = 720;
  int sequence = 1;

  video_source_.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(0, stats.number_of_cpu_adapt_changes);

  // Trigger CPU overuse, should now adapt down.
  video_stream_encoder_->TriggerCpuOveruse();
  video_source_.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_TRUE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Set new source with adaptation still enabled.
  test::FrameForwarder new_video_source;
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  new_video_source.IncomingCapturedFrame(
      CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_TRUE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Set cpu adaptation by frame dropping.
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);
  new_video_source.IncomingCapturedFrame(
      CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  // Not adapted at first.
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(1, stats.number_of_cpu_adapt_changes);

  // Force an input frame rate to be available, or the adaptation call won't
  // know what framerate to adapt from.
  VideoSendStream::Stats mock_stats = stats_proxy_->GetStats();
  mock_stats.input_frame_rate = 30;
  stats_proxy_->SetMockStats(mock_stats);
  video_stream_encoder_->TriggerCpuOveruse();
  stats_proxy_->ResetMockStats();

  new_video_source.IncomingCapturedFrame(
      CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);

  // Framerate now adapted.
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_TRUE(stats.cpu_limited_framerate);
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);

  // Disable CPU adaptation.
  video_stream_encoder_->SetSource(
      &new_video_source,
      DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION);
  new_video_source.IncomingCapturedFrame(
      CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);

  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);

  // Try to trigger overuse. Should not succeed.
  stats_proxy_->SetMockStats(mock_stats);
  video_stream_encoder_->TriggerCpuOveruse();
  stats_proxy_->ResetMockStats();

  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);

  // Switch back the source with resolution adaptation enabled.
  video_stream_encoder_->SetSource(&video_source_,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  video_source_.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_TRUE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(2, stats.number_of_cpu_adapt_changes);

  // Trigger CPU normal usage.
  video_stream_encoder_->TriggerCpuUnderuse();
  video_source_.IncomingCapturedFrame(CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(3, stats.number_of_cpu_adapt_changes);

  // Back to the source with adaptation off, set it back to maintain-resolution.
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);
  new_video_source.IncomingCapturedFrame(
      CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  // Disabled, since we previously switched the source to disabled.
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_TRUE(stats.cpu_limited_framerate);
  EXPECT_EQ(3, stats.number_of_cpu_adapt_changes);

  // Trigger CPU normal usage.
  video_stream_encoder_->TriggerCpuUnderuse();
  new_video_source.IncomingCapturedFrame(
      CreateFrame(sequence, kWidth, kHeight));
  WaitForEncodedFrame(sequence++);
  stats = stats_proxy_->GetStats();
  EXPECT_FALSE(stats.cpu_limited_resolution);
  EXPECT_FALSE(stats.cpu_limited_framerate);
  EXPECT_EQ(4, stats.number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats.number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ScalingUpAndDownDoesNothingWithMaintainResolution) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Expect no scaling to begin with.
  EXPECT_THAT(video_source_.sink_wants(), UnlimitedSinkWants());

  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(1);

  // Trigger scale down.
  video_stream_encoder_->TriggerQualityLow();

  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  WaitForEncodedFrame(2);

  // Expect a scale down.
  EXPECT_TRUE(video_source_.sink_wants().max_pixel_count);
  EXPECT_LT(video_source_.sink_wants().max_pixel_count, kWidth * kHeight);

  // Set resolution scaling disabled.
  test::FrameForwarder new_video_source;
  video_stream_encoder_->SetSource(&new_video_source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);

  // Trigger scale down.
  video_stream_encoder_->TriggerQualityLow();
  new_video_source.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  WaitForEncodedFrame(3);

  // Expect no scaling.
  EXPECT_EQ(std::numeric_limits<int>::max(),
            new_video_source.sink_wants().max_pixel_count);

  // Trigger scale up.
  video_stream_encoder_->TriggerQualityHigh();
  new_video_source.IncomingCapturedFrame(CreateFrame(4, kWidth, kHeight));
  WaitForEncodedFrame(4);

  // Expect nothing to change, still no scaling.
  EXPECT_EQ(std::numeric_limits<int>::max(),
            new_video_source.sink_wants().max_pixel_count);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       SkipsSameAdaptDownRequest_MaintainFramerateMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  source.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(1);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerCpuOveruse();
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  const int kLastMaxPixelCount = source.sink_wants().max_pixel_count;
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt down for same input resolution, expect no change.
  video_stream_encoder_->TriggerCpuOveruse();
  EXPECT_EQ(kLastMaxPixelCount, source.sink_wants().max_pixel_count);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SkipsSameOrLargerAdaptDownRequest_BalancedMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);
  source.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  sink_.WaitForEncodedFrame(1);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  const int kLastMaxPixelCount = source.sink_wants().max_pixel_count;

  // Trigger adapt down for same input resolution, expect no change.
  source.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  sink_.WaitForEncodedFrame(2);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_EQ(kLastMaxPixelCount, source.sink_wants().max_pixel_count);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down for larger input resolution, expect no change.
  source.IncomingCapturedFrame(CreateFrame(3, kWidth + 1, kHeight + 1));
  sink_.WaitForEncodedFrame(3);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_EQ(kLastMaxPixelCount, source.sink_wants().max_pixel_count);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       FpsCountReturnsToZeroForFewerAdaptationsUpThanDown) {
  const int64_t kFrameInterval150Ms = 150;
  const int kWidth = 640;
  const int kHeight = 360;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);

  int64_t timestamp_ms = kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect reduced fps (640x360@15fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsMatchesResolutionMax(Lt(kDefaultFramerate)));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Source requests 270p, expect reduced resolution (480x270@15fps).
  source.OnOutputFormatRequest(480, 270);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(480, 270);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect reduced fps (480x270@10fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Source requests QVGA, expect reduced resolution (320x180@10fps).
  source.OnOutputFormatRequest(320, 180);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(320, 180);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect reduced fps (320x180@7fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Source requests VGA, expect increased resolution (640x360@7fps).
  source.OnOutputFormatRequest(640, 360);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect increased fps (640x360@(max-2)fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect increased fps (640x360@(max-1)fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect increased fps (640x360@maxfps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(6, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       FpsCountReturnsToZeroForFewerAdaptationsUpThanDownWithTwoResources) {
  const int kWidth = 1280;
  const int kHeight = 720;
  const int64_t kFrameInterval150Ms = 150;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);

  int64_t timestamp_ms = kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (960x540@maxfps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (640x360@maxfps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionLt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect reduced fps (640x360@15fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Source requests QVGA, expect reduced resolution (320x180@15fps).
  source.OnOutputFormatRequest(320, 180);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(320, 180);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt down, expect reduced fps (320x180@7fps).
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Source requests HD, expect increased resolution (640x360@7fps).
  source.OnOutputFormatRequest(1280, 720);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect increased fps (640x360@(max-1)fps).
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect increased fps (640x360@maxfps).
  video_stream_encoder_->TriggerQualityHigh();
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect increased resolution (960x570@maxfps).
  video_stream_encoder_->TriggerQualityHigh();
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionGt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect increased resolution (1280x720@maxfps).
  video_stream_encoder_->TriggerQualityHigh();
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionGt(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(6, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       NoChangeForInitialNormalUsage_MaintainFramerateMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  source.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect no change.
  video_stream_encoder_->TriggerCpuUnderuse();
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       NoChangeForInitialNormalUsage_MaintainResolutionMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable MAINTAIN_RESOLUTION preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);

  source.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect no change.
  video_stream_encoder_->TriggerCpuUnderuse();
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NoChangeForInitialNormalUsage_BalancedMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);

  source.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no change.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NoChangeForInitialNormalUsage_DisabledMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable DISABLED preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(
      &source, DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION);

  source.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no change.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptsResolutionForLowQuality_MaintainFramerateMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  source.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  WaitForEncodedFrame(1);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerQualityLow();
  source.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  WaitForEncodedFrame(2);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no restriction.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptsFramerateForLowQuality_MaintainResolutionMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  const int kInputFps = 30;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kInputFps;
  stats_proxy_->SetMockStats(stats);

  // Expect no scaling to begin with (preference: MAINTAIN_FRAMERATE).
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  sink_.WaitForEncodedFrame(1);
  EXPECT_THAT(video_source_.sink_wants(), UnlimitedSinkWants());

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerQualityLow();
  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  sink_.WaitForEncodedFrame(2);
  EXPECT_THAT(video_source_.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));

  // Enable MAINTAIN_RESOLUTION preference.
  test::FrameForwarder new_video_source;
  video_stream_encoder_->SetSourceAndWaitForRestrictionsUpdated(
      &new_video_source, DegradationPreference::MAINTAIN_RESOLUTION);
  // Give the encoder queue time to process the change in degradation preference
  // by waiting for an encoded frame.
  new_video_source.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  sink_.WaitForEncodedFrame(3);
  EXPECT_THAT(new_video_source.sink_wants(), UnlimitedSinkWants());

  // Trigger adapt down, expect reduced framerate.
  video_stream_encoder_->TriggerQualityLow();
  new_video_source.IncomingCapturedFrame(CreateFrame(4, kWidth, kHeight));
  sink_.WaitForEncodedFrame(4);
  EXPECT_THAT(new_video_source.sink_wants(),
              FpsMatchesResolutionMax(Lt(kInputFps)));

  // Trigger adapt up, expect no restriction.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(new_video_source.sink_wants(), UnlimitedSinkWants());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DoesNotScaleBelowSetResolutionLimit) {
  const int kWidth = 1280;
  const int kHeight = 720;
  const size_t kNumFrames = 10;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable adapter, expected input resolutions when downscaling:
  // 1280x720 -> 960x540 -> 640x360 -> 480x270 -> 320x180 (kMinPixelsPerFrame)
  video_source_.set_adaptation_enabled(true);

  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  int downscales = 0;
  for (size_t i = 1; i <= kNumFrames; i++) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(i * kFrameIntervalMs, kWidth, kHeight));
    WaitForEncodedFrame(i * kFrameIntervalMs);

    // Trigger scale down.
    VideoSinkWants last_wants = video_source_.sink_wants();
    video_stream_encoder_->TriggerQualityLow();
    EXPECT_GE(video_source_.sink_wants().max_pixel_count, kMinPixelsPerFrame);

    if (video_source_.sink_wants().max_pixel_count < last_wants.max_pixel_count)
      ++downscales;

    EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
    EXPECT_EQ(downscales,
              stats_proxy_->GetStats().number_of_quality_adapt_changes);
    EXPECT_GT(downscales, 0);
  }
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptsResolutionUpAndDownTwiceOnOveruse_MaintainFramerateMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  int64_t timestamp_ms = kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect no restriction.
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  // Trigger adapt up, expect no restriction.
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptsResolutionUpAndDownTwiceForLowQuality_BalancedMode_NoFpsLimit) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);

  int64_t timestamp_ms = kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no restriction.
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution.
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no restriction.
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  sink_.WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, AdaptUpIfBwEstimateIsHigherThanMinBitrate) {
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderBitrateLimits540p, kEncoderBitrateLimits720p});

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kEncoderBitrateLimits720p.min_start_bitrate_bps),
      DataRate::BitsPerSec(kEncoderBitrateLimits720p.min_start_bitrate_bps), 0,
      0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  // Insert 720p frame.
  int64_t timestamp_ms = kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(1280, 720);

  // Reduce bitrate and trigger adapt down.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kEncoderBitrateLimits540p.min_start_bitrate_bps),
      DataRate::BitsPerSec(kEncoderBitrateLimits540p.min_start_bitrate_bps), 0,
      0, 0);
  video_stream_encoder_->TriggerQualityLow();

  // Insert 720p frame. It should be downscaled and encoded.
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(960, 540);

  // Trigger adapt up. Higher resolution should not be requested duo to lack
  // of bitrate.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(1280 * 720)));

  // Increase bitrate.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kEncoderBitrateLimits720p.min_start_bitrate_bps),
      DataRate::BitsPerSec(kEncoderBitrateLimits720p.min_start_bitrate_bps), 0,
      0, 0);

  // Trigger adapt up. Higher resolution should be requested.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropFirstFramesIfBwEstimateIsTooLow) {
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderBitrateLimits540p, kEncoderBitrateLimits720p});

  // Set bitrate equal to min bitrate of 540p.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kEncoderBitrateLimits540p.min_start_bitrate_bps),
      DataRate::BitsPerSec(kEncoderBitrateLimits540p.min_start_bitrate_bps), 0,
      0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  // Insert 720p frame. It should be dropped and lower resolution should be
  // requested.
  int64_t timestamp_ms = kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  ExpectDroppedFrame();
  EXPECT_THAT(WaitUntil([&] { return source.sink_wants().max_pixel_count; },
                        Lt(1280 * 720)),
              IsRtcOk());

  // Insert 720p frame. It should be downscaled and encoded.
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(960, 540);

  video_stream_encoder_->Stop();
}

class BalancedDegradationTest : public VideoStreamEncoderTest {
 protected:
  void SetupTestWithFieldTrial(absl::string_view key, absl::string_view value) {
    extra_trials_ = std::make_unique<FieldTrials>(field_trials_);
    extra_trials_->Set(key, value);
    // Reset encoder for field trials to take effect.
    ConfigureEncoder(video_encoder_config_.Copy(),
                     VideoStreamEncoder::BitrateAllocationCallbackType::
                         kVideoLayersAllocation,
                     /* num_cores= */ 1, extra_trials_.get());
    OnBitrateUpdated(kTargetBitrate);

    // Enable BALANCED preference.
    source_.set_adaptation_enabled(true);
    video_stream_encoder_->SetSource(&source_, DegradationPreference::BALANCED);
  }

  void OnBitrateUpdated(DataRate bitrate) {
    video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
        bitrate, bitrate, 0, 0, 0);
  }

  void InsertFrame() {
    timestamp_ms_ += kFrameIntervalMs;
    source_.IncomingCapturedFrame(CreateFrame(timestamp_ms_, kWidth, kHeight));
  }

  void InsertFrameAndWaitForEncoded() {
    InsertFrame();
    sink_.WaitForEncodedFrame(timestamp_ms_);
  }

  const int kWidth = 640;  // pixels:640x360=230400
  const int kHeight = 360;
  const int64_t kFrameIntervalMs = 150;  // Use low fps to not drop any frame.
  int64_t timestamp_ms_ = 0;
  AdaptingFrameForwarder source_{&time_controller_};
  std::unique_ptr<FieldTrials> extra_trials_;
};

TEST_F(BalancedDegradationTest, AdaptDownTwiceIfMinFpsDiffLtThreshold) {
  SetupTestWithFieldTrial(
      "WebRTC-Video-BalancedDegradationSettings",
      "pixels:57600|129600|230400,fps:7|10|24,fps_diff:1|1|1");

  // Force input frame rate.
  const int kInputFps = 24;
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kInputFps;
  stats_proxy_->SetMockStats(stats);

  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), UnlimitedSinkWants());

  // Trigger adapt down, expect scaled down framerate and resolution,
  // since Fps diff (input-requested:0) < threshold.
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_THAT(source_.sink_wants(),
              AllOf(WantsFps(Eq(24)), WantsMaxPixels(Le(230400))));

  video_stream_encoder_->Stop();
}

TEST_F(BalancedDegradationTest, AdaptDownOnceIfFpsDiffGeThreshold) {
  SetupTestWithFieldTrial(
      "WebRTC-Video-BalancedDegradationSettings",
      "pixels:57600|129600|230400,fps:7|10|24,fps_diff:1|1|1");

  // Force input frame rate.
  const int kInputFps = 25;
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kInputFps;
  stats_proxy_->SetMockStats(stats);

  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), UnlimitedSinkWants());

  // Trigger adapt down, expect scaled down framerate only (640x360@24fps).
  // Fps diff (input-requested:1) == threshold.
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_THAT(source_.sink_wants(), FpsMatchesResolutionMax(Eq(24)));

  video_stream_encoder_->Stop();
}

TEST_F(BalancedDegradationTest, AdaptDownUsesCodecSpecificFps) {
  SetupTestWithFieldTrial(
      "WebRTC-Video-BalancedDegradationSettings",
      "pixels:57600|129600|230400,fps:7|10|24,vp8_fps:8|11|22");

  EXPECT_EQ(kVideoCodecVP8, video_encoder_config_.codec_type);

  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), UnlimitedSinkWants());

  // Trigger adapt down, expect scaled down framerate (640x360@22fps).
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_THAT(source_.sink_wants(), FpsMatchesResolutionMax(Eq(22)));

  video_stream_encoder_->Stop();
}

TEST_F(BalancedDegradationTest, NoAdaptUpIfBwEstimateIsLessThanMinBitrate) {
  SetupTestWithFieldTrial(
      "WebRTC-Video-BalancedDegradationSettings",
      "pixels:57600|129600|230400,fps:7|10|14,kbps:0|0|425");

  const DataRate kMinBitrate = DataRate::KilobitsPerSec(425);
  const DataRate kTooLowMinBitrate = DataRate::KilobitsPerSec(424);
  OnBitrateUpdated(kTooLowMinBitrate);

  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), UnlimitedSinkWants());
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down framerate (640x360@14fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsMatchesResolutionMax(Eq(14)));
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (480x270@14fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsEqResolutionLt(source_.last_wants()));
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down framerate (480x270@10fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsLtResolutionEq(source_.last_wants()));
  EXPECT_EQ(source_.sink_wants().max_framerate_fps, 10);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no upscale in fps (target bitrate < min bitrate).
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled fps (target bitrate == min bitrate).
  OnBitrateUpdated(kMinBitrate);
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_EQ(source_.sink_wants().max_framerate_fps, 14);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(BalancedDegradationTest,
       InitialFrameDropAdaptsFpsAndResolutionInOneStep) {
  SetupTestWithFieldTrial("WebRTC-Video-BalancedDegradationSettings",
                          "pixels:57600|129600|230400,fps:7|24|24");
  OnBitrateUpdated(kLowTargetBitrate);

  EXPECT_THAT(source_.sink_wants(), UnlimitedSinkWants());

  // Insert frame, expect scaled down:
  // framerate (640x360@24fps) -> resolution (480x270@24fps).
  InsertFrame();
  EXPECT_FALSE(WaitForFrame(TimeDelta::Seconds(1)));
  EXPECT_LT(source_.sink_wants().max_pixel_count, kWidth * kHeight);
  EXPECT_EQ(source_.sink_wants().max_framerate_fps, 24);

  // Insert frame, expect scaled down:
  // resolution (320x180@24fps).
  InsertFrame();
  EXPECT_FALSE(WaitForFrame(TimeDelta::Seconds(1)));
  EXPECT_LT(source_.sink_wants().max_pixel_count,
            source_.last_wants().max_pixel_count);
  EXPECT_EQ(source_.sink_wants().max_framerate_fps, 24);

  // Frame should not be dropped (min pixels per frame reached).
  InsertFrameAndWaitForEncoded();

  video_stream_encoder_->Stop();
}

TEST_F(BalancedDegradationTest,
       NoAdaptUpInResolutionIfBwEstimateIsLessThanMinBitrate) {
  SetupTestWithFieldTrial(
      "WebRTC-Video-BalancedDegradationSettings",
      "pixels:57600|129600|230400,fps:7|10|14,kbps_res:0|0|435");

  const DataRate kResolutionMinBitrate = DataRate::KilobitsPerSec(435);
  const DataRate kTooLowMinResolutionBitrate = DataRate::KilobitsPerSec(434);
  OnBitrateUpdated(kTooLowMinResolutionBitrate);

  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), UnlimitedSinkWants());
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down framerate (640x360@14fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsMatchesResolutionMax(Eq(14)));
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (480x270@14fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsEqResolutionLt(source_.last_wants()));
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down framerate (480x270@10fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsLtResolutionEq(source_.last_wants()));
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled fps (no bitrate limit) (480x270@14fps).
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsGtResolutionEq(source_.last_wants()));
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no upscale in res (target bitrate < min bitrate).
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled res (target bitrate == min bitrate).
  OnBitrateUpdated(kResolutionMinBitrate);
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsEqResolutionGt(source_.last_wants()));
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(BalancedDegradationTest,
       NoAdaptUpInFpsAndResolutionIfBwEstimateIsLessThanMinBitrate) {
  SetupTestWithFieldTrial(
      "WebRTC-Video-BalancedDegradationSettings",
      "pixels:57600|129600|230400,fps:7|10|14,kbps:0|0|425,kbps_res:0|0|435");

  const DataRate kMinBitrate = DataRate::KilobitsPerSec(425);
  const DataRate kTooLowMinBitrate = DataRate::KilobitsPerSec(424);
  const DataRate kResolutionMinBitrate = DataRate::KilobitsPerSec(435);
  const DataRate kTooLowMinResolutionBitrate = DataRate::KilobitsPerSec(434);
  OnBitrateUpdated(kTooLowMinBitrate);

  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), UnlimitedSinkWants());
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down framerate (640x360@14fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsMatchesResolutionMax(Eq(14)));
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (480x270@14fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsEqResolutionLt(source_.last_wants()));
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down framerate (480x270@10fps).
  video_stream_encoder_->TriggerQualityLow();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsLtResolutionEq(source_.last_wants()));
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no upscale (target bitrate < min bitrate).
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled fps (target bitrate == min bitrate).
  OnBitrateUpdated(kMinBitrate);
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsGtResolutionEq(source_.last_wants()));
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no upscale in res (target bitrate < min bitrate).
  OnBitrateUpdated(kTooLowMinResolutionBitrate);
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled res (target bitrate == min bitrate).
  OnBitrateUpdated(kResolutionMinBitrate);
  video_stream_encoder_->TriggerQualityHigh();
  InsertFrameAndWaitForEncoded();
  EXPECT_THAT(source_.sink_wants(), FpsEqResolutionGt(source_.last_wants()));
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptsResolutionOnOveruseAndLowQuality_MaintainFramerateMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  int64_t timestamp_ms = kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt down, expect scaled down resolution (960x540).
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt down, expect scaled down resolution (640x360).
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionLt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt down, expect scaled down resolution (480x270).
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionLt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt down, expect scaled down resolution (320x180).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionLt(source.last_wants()));
  VideoSinkWants last_wants = source.sink_wants();
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt down, expect no change (min resolution reached).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsUnlimited());
  EXPECT_EQ(source.sink_wants().max_pixel_count, last_wants.max_pixel_count);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt up, expect upscaled resolution (480x270).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionGt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality and cpu adapt up since both are most limited, expect
  // upscaled resolution (640x360).
  video_stream_encoder_->TriggerCpuUnderuse();
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionGt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality and cpu adapt up since both are most limited, expect
  // upscaled resolution (960x540).
  video_stream_encoder_->TriggerCpuUnderuse();
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionGt(source.last_wants()));
  last_wants = source.sink_wants();
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt up, expect no change since not most limited (960x540).
  // However the stats will change since the CPU resource is no longer limited.
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionEqTo(last_wants));
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(6, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt up, expect no restriction (1280x720).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameIntervalMs;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(),
              FpsUnlimitedResolutionGt(source.last_wants()));
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_EQ(6, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, CpuLimitedHistogramIsReported) {
  const int kWidth = 640;
  const int kHeight = 360;
  int64_t ntp_timestamp_ms = 123;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  for (int i = 1; i <= SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(ntp_timestamp_ms, kWidth, kHeight));
    WaitForEncodedFrame(ntp_timestamp_ms);
    ntp_timestamp_ms += 20;
  }

  video_stream_encoder_->TriggerCpuOveruse();
  for (int i = 1; i <= SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(ntp_timestamp_ms, kWidth, kHeight));
    WaitForEncodedFrame(ntp_timestamp_ms);
    ntp_timestamp_ms += 20;
  }

  video_stream_encoder_->Stop();
  video_stream_encoder_.reset();
  stats_proxy_.reset();

  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.Video.CpuLimitedResolutionInPercent"));
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.Video.CpuLimitedResolutionInPercent", 50));
}

TEST_F(VideoStreamEncoderTest,
       CpuLimitedHistogramIsNotReportedForDisabledDegradation) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  const int kWidth = 640;
  const int kHeight = 360;
  int64_t ntp_timestamp_ms = 123;

  video_stream_encoder_->SetSource(
      &video_source_, DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION);

  for (int i = 1; i <= SendStatisticsProxy::kMinRequiredMetricsSamples; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(ntp_timestamp_ms, kWidth, kHeight));
    WaitForEncodedFrame(ntp_timestamp_ms);
    ntp_timestamp_ms += 20;
  }

  video_stream_encoder_->Stop();
  video_stream_encoder_.reset();
  stats_proxy_.reset();

  EXPECT_EQ(0,
            metrics::NumSamples("WebRTC.Video.CpuLimitedResolutionInPercent"));
}

TEST_F(VideoStreamEncoderTest, ReportsVideoLayersAllocationForVP8Simulcast) {
  ResetEncoder("VP8", /*num_streams*/ 2, 1, 1, /*screenshare*/ false,
               kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);

  const int kDefaultFps = 30;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kLowTargetBitrate, kLowTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();
  // kLowTargetBitrate is only enough for one spatial layer.
  ASSERT_EQ(last_layer_allocation.active_spatial_layers.size(), 1u);

  VideoBitrateAllocation bitrate_allocation =
      fake_encoder_.GetAndResetLastRateControlSettings()->target_bitrate;
  // Check that encoder has been updated too, not just allocation observer.
  EXPECT_EQ(bitrate_allocation.get_sum_bps(), kLowTargetBitrate.bps());
  AdvanceTime(TimeDelta::Seconds(1) / kDefaultFps);

  // VideoLayersAllocation might be updated if frame rate changes.
  int number_of_layers_allocation = 1;
  const int64_t start_time_ms = CurrentTimeMs();
  while (CurrentTimeMs() - start_time_ms < 10 * kProcessIntervalMs) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
    WaitForEncodedFrame(CurrentTimeMs());
    if (number_of_layers_allocation != sink_.number_of_layers_allocations()) {
      number_of_layers_allocation = sink_.number_of_layers_allocations();
      VideoLayersAllocation new_allocation =
          sink_.GetLastVideoLayersAllocation();
      ASSERT_EQ(new_allocation.active_spatial_layers.size(), 1u);
      EXPECT_NE(new_allocation.active_spatial_layers[0].frame_rate_fps,
                last_layer_allocation.active_spatial_layers[0].frame_rate_fps);
      EXPECT_EQ(new_allocation.active_spatial_layers[0]
                    .target_bitrate_per_temporal_layer,
                last_layer_allocation.active_spatial_layers[0]
                    .target_bitrate_per_temporal_layer);
      last_layer_allocation = new_allocation;
    }
  }
  EXPECT_LE(sink_.number_of_layers_allocations(), 3);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForVP8WithMiddleLayerDisabled) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 2, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP8,
                                 /* num_streams*/ 3, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp8EncoderSpecificSettings>(
          VideoEncoder::GetDefaultVp8Settings());
  for (auto& layer : video_encoder_config.simulcast_layers) {
    layer.num_temporal_layers = 2;
  }
  // Simulcast layers are used for enabling/disabling streams.
  video_encoder_config.simulcast_layers[0].active = true;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = true;
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(2));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_LT(last_layer_allocation.active_spatial_layers[0].width, 1280);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].width, 1280);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForVP8WithMiddleAndHighestLayerDisabled) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 2, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP8,
                                 /* num_streams*/ 3, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp8EncoderSpecificSettings>(
          VideoEncoder::GetDefaultVp8Settings());
  for (auto& layer : video_encoder_config.simulcast_layers) {
    layer.num_temporal_layers = 2;
  }
  // Simulcast layers are used for enabling/disabling streams.
  video_encoder_config.simulcast_layers[0].active = true;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = false;
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(1));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_LT(last_layer_allocation.active_spatial_layers[0].width, 1280);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForV9SvcWithTemporalLayerSupport) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP9,
                                 /* num_streams*/ 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 2;
  vp9_settings.numberOfTemporalLayers = 2;
  vp9_settings.interLayerPred = InterLayerPredMode::kOn;
  vp9_settings.automaticResizeOn = false;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(2));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].width, 640);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].height, 360);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].frame_rate_fps, 30);
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[1]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].width, 1280);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].height, 720);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].frame_rate_fps, 30);

  // Since full SVC is used, expect the top layer to utilize the full target
  // rate.
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1]
                .target_bitrate_per_temporal_layer[1],
            kTargetBitrate);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForV9SvcWithoutTemporalLayerSupport) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, false);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, false);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP9,
                                 /* num_streams*/ 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 2;
  vp9_settings.numberOfTemporalLayers = 2;
  vp9_settings.interLayerPred = InterLayerPredMode::kOn;
  vp9_settings.automaticResizeOn = false;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(2));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(1));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[1]
                  .target_bitrate_per_temporal_layer,
              SizeIs(1));
  // Since full SVC is used, expect the top layer to utilize the full target
  // rate.
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1]
                .target_bitrate_per_temporal_layer[0],
            kTargetBitrate);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForVP9KSvcWithTemporalLayerSupport) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP9,
                                 /* num_streams*/ 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 2;
  vp9_settings.numberOfTemporalLayers = 2;
  vp9_settings.interLayerPred = InterLayerPredMode::kOnKeyPic;
  vp9_settings.automaticResizeOn = false;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(2));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[1]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  // Since  KSVC is, spatial layers are independend except on key frames.
  EXPECT_LT(last_layer_allocation.active_spatial_layers[1]
                .target_bitrate_per_temporal_layer[1],
            kTargetBitrate);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForV9SvcWithLowestLayerDisabled) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 2, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP9,
                                 /* num_streams*/ 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 3;
  vp9_settings.numberOfTemporalLayers = 2;
  vp9_settings.interLayerPred = InterLayerPredMode::kOn;
  vp9_settings.automaticResizeOn = false;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  // Simulcast layers are used for enabling/disabling streams.
  video_encoder_config.simulcast_layers.resize(3);
  video_encoder_config.simulcast_layers[0].active = false;
  video_encoder_config.simulcast_layers[1].active = true;
  video_encoder_config.simulcast_layers[2].active = true;
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(2));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].width, 640);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].spatial_id, 0);

  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].width, 1280);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].spatial_id, 1);
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[1]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  // Since full SVC is used, expect the top layer to utilize the full target
  // rate.
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1]
                .target_bitrate_per_temporal_layer[1],
            kTargetBitrate);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForV9SvcWithHighestLayerDisabled) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 2, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP9,
                                 /* num_streams*/ 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 3;
  vp9_settings.numberOfTemporalLayers = 2;
  vp9_settings.interLayerPred = InterLayerPredMode::kOn;
  vp9_settings.automaticResizeOn = false;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  // Simulcast layers are used for enabling/disabling streams.
  video_encoder_config.simulcast_layers.resize(3);
  video_encoder_config.simulcast_layers[2].active = false;
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(2));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].width, 320);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].spatial_id, 0);

  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].width, 640);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[1].spatial_id, 1);
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[1]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsVideoLayersAllocationForV9SvcWithAllButHighestLayerDisabled) {
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 2, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP9,
                                 /* num_streams*/ 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 3;
  vp9_settings.numberOfTemporalLayers = 2;
  vp9_settings.interLayerPred = InterLayerPredMode::kOn;
  vp9_settings.automaticResizeOn = false;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  // Simulcast layers are used for enabling/disabling streams.
  video_encoder_config.simulcast_layers.resize(3);
  video_encoder_config.simulcast_layers[0].active = false;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = true;
  ConfigureEncoder(std::move(video_encoder_config),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(1));
  EXPECT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(2));
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].width, 1280);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].spatial_id, 0);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0]
                .target_bitrate_per_temporal_layer[1],
            kTargetBitrate);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, ReportsVideoLayersAllocationForH264) {
  ResetEncoder("H264", 1, 1, 1, false, kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();

  ASSERT_THAT(last_layer_allocation.active_spatial_layers, SizeIs(1));
  ASSERT_THAT(last_layer_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(1));
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0]
                .target_bitrate_per_temporal_layer[0],
            kTargetBitrate);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].width, 1280);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].height, 720);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0].frame_rate_fps, 30);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsUpdatedVideoLayersAllocationWhenBweChanges) {
  ResetEncoder("VP8", /*num_streams*/ 2, 1, 1, /*screenshare*/ false,
               kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kLowTargetBitrate, kLowTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  VideoLayersAllocation last_layer_allocation =
      sink_.GetLastVideoLayersAllocation();
  // kLowTargetBitrate is only enough for one spatial layer.
  ASSERT_EQ(last_layer_allocation.active_spatial_layers.size(), 1u);
  EXPECT_EQ(last_layer_allocation.active_spatial_layers[0]
                .target_bitrate_per_temporal_layer[0],
            kLowTargetBitrate);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
  WaitForEncodedFrame(CurrentTimeMs());

  EXPECT_EQ(sink_.number_of_layers_allocations(), 2);
  last_layer_allocation = sink_.GetLastVideoLayersAllocation();
  ASSERT_EQ(last_layer_allocation.active_spatial_layers.size(), 2u);
  EXPECT_GT(last_layer_allocation.active_spatial_layers[1]
                .target_bitrate_per_temporal_layer[0],
            DataRate::Zero());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ReportsUpdatedVideoLayersAllocationWhenResolutionChanges) {
  ResetEncoder("VP8", /*num_streams*/ 2, 1, 1, /*screenshare*/ false,
               kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(CurrentTimeMs(), codec_width_, codec_height_));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);
  ASSERT_THAT(sink_.GetLastVideoLayersAllocation().active_spatial_layers,
              SizeIs(2));
  EXPECT_EQ(sink_.GetLastVideoLayersAllocation().active_spatial_layers[1].width,
            codec_width_);
  EXPECT_EQ(
      sink_.GetLastVideoLayersAllocation().active_spatial_layers[1].height,
      codec_height_);

  video_source_.IncomingCapturedFrame(
      CreateFrame(CurrentTimeMs(), codec_width_ / 2, codec_height_ / 2));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(sink_.number_of_layers_allocations(), 2);
  ASSERT_THAT(sink_.GetLastVideoLayersAllocation().active_spatial_layers,
              SizeIs(2));
  EXPECT_EQ(sink_.GetLastVideoLayersAllocation().active_spatial_layers[1].width,
            codec_width_ / 2);
  EXPECT_EQ(
      sink_.GetLastVideoLayersAllocation().active_spatial_layers[1].height,
      codec_height_ / 2);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, TemporalLayersNotDisabledIfSupported) {
  // 2 TLs configured, temporal layers supported by encoder.
  const int kNumTemporalLayers = 2;
  ResetEncoder("VP8", 1, kNumTemporalLayers, 1, /*screenshare*/ false,
               kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);
  fake_encoder_.SetTemporalLayersSupported(0, true);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  WaitForEncodedFrame(1);

  // Bitrate allocated across temporal layers.
  const DataRate kTl0 =
      kTargetBitrate * SimulcastRateAllocator::GetTemporalRateAllocation(
                           kNumTemporalLayers, /*temporal_id=*/0,
                           /*base_heavy_tl3_alloc=*/false);
  const DataRate kTl1 =
      kTargetBitrate * SimulcastRateAllocator::GetTemporalRateAllocation(
                           kNumTemporalLayers, /*temporal_id=*/1,
                           /*base_heavy_tl3_alloc=*/false);

  VideoLayersAllocation allocation = sink_.GetLastVideoLayersAllocation();
  ASSERT_THAT(allocation.active_spatial_layers, SizeIs(1));
  EXPECT_THAT(
      allocation.active_spatial_layers[0].target_bitrate_per_temporal_layer,
      ElementsAre(kTl0, kTl1));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, TemporalLayersDisabledIfNotSupported) {
  // 2 TLs configured, temporal layers not supported by encoder.
  ResetEncoder("VP8", 1, /*num_temporal_layers*/ 2, 1, /*screenshare*/ false,
               kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);
  fake_encoder_.SetTemporalLayersSupported(0, false);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  WaitForEncodedFrame(1);

  // Temporal layers not supported by the encoder.
  // Total bitrate should be at ti:0.
  VideoLayersAllocation allocation = sink_.GetLastVideoLayersAllocation();
  ASSERT_THAT(allocation.active_spatial_layers, SizeIs(1));
  EXPECT_THAT(
      allocation.active_spatial_layers[0].target_bitrate_per_temporal_layer,
      ElementsAre(kTargetBitrate));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, VerifyBitrateAllocationForTwoStreams) {
  auto field_trials = SetFieldTrial(
      "WebRTC-Video-QualityScalerSettings",
      "initial_bitrate_interval_ms:1000,initial_bitrate_factor:0.2");
  // Reset encoder for field trials to take effect.
  ConfigureEncoder(video_encoder_config_.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
                   /* num_cores= */ 1, &field_trials);

  // 2 TLs configured, temporal layers only supported for first stream.
  ResetEncoder("VP8", 2, /*num_temporal_layers*/ 2, 1, /*screenshare*/ false,
               kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);
  fake_encoder_.SetTemporalLayersSupported(0, true);
  fake_encoder_.SetTemporalLayersSupported(1, false);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  WaitForEncodedFrame(1);

  const DataRate kS0 = DataRate::BitsPerSec(150'000);
  const DataRate kS0Tl0 =
      kS0 *
      SimulcastRateAllocator::GetTemporalRateAllocation(
          /*num_layers=*/2, /*temporal_id=*/0, /*base_heavy_tl3_alloc=*/false);
  const DataRate kS0Tl1 =
      kS0 *
      SimulcastRateAllocator::GetTemporalRateAllocation(
          /*num_layers=*/2, /*temporal_id=*/1, /*base_heavy_tl3_alloc=*/false);
  const DataRate kS1 = kTargetBitrate - kS0Tl1;
  // Temporal layers not supported by si:1.
  VideoLayersAllocation allocation = sink_.GetLastVideoLayersAllocation();
  ASSERT_THAT(allocation.active_spatial_layers, SizeIs(2));
  EXPECT_THAT(
      allocation.active_spatial_layers[0].target_bitrate_per_temporal_layer,
      ElementsAre(kS0Tl0, kS0Tl1));
  EXPECT_THAT(
      allocation.active_spatial_layers[1].target_bitrate_per_temporal_layer,
      ElementsAre(kS1));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncodeUsageResourceDisabledWhenCpuOveruseDetectionDisabled) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  fake_encoder_.SetEnableCpuOveruseDetection(false);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = kTargetBitrate.bps();
  ConfigureEncoder(std::move(video_encoder_config));

  video_source_.IncomingCapturedFrame(
      CreateFrame(1, kFrameWidth, kFrameHeight));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      -1);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncodeUsageResourceStoppedWhenCpuOveruseDetectionDisabled) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(1, kFrameWidth, kFrameHeight));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  const int last_framerate =
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate();
  EXPECT_NE(last_framerate, -1);

  fake_encoder_.SetEnableCpuOveruseDetection(false);
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_stream_encoder_->overuse_detector_proxy_->ConsumeFramerateUpdated();
  video_source_.IncomingCapturedFrame(
      CreateFrame(2, kFrameWidth, kFrameHeight));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_FALSE(video_stream_encoder_->overuse_detector_proxy_
                   ->ConsumeFramerateUpdated());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncodeUsageResourceRestartsWhenCpuOveruseDetectionReEnabled) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(
      CreateFrame(1, kFrameWidth, kFrameHeight));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_NE(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      -1);

  fake_encoder_.SetEnableCpuOveruseDetection(false);
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_stream_encoder_->overuse_detector_proxy_->ConsumeFramerateUpdated();
  video_source_.IncomingCapturedFrame(
      CreateFrame(2, kFrameWidth, kFrameHeight));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_FALSE(video_stream_encoder_->overuse_detector_proxy_
                   ->ConsumeFramerateUpdated());

  fake_encoder_.SetEnableCpuOveruseDetection(true);
  video_encoder_config = video_encoder_config_.Copy();
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_stream_encoder_->overuse_detector_proxy_->ConsumeFramerateUpdated();
  video_source_.IncomingCapturedFrame(
      CreateFrame(3, kFrameWidth, kFrameHeight));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_TRUE(video_stream_encoder_->overuse_detector_proxy_
                  ->ConsumeFramerateUpdated());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, OveruseDetectorUpdatedOnReconfigureAndAdaption) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  const int kFramerate = 24;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);

  // Insert a single frame, triggering initial configuration.
  source.IncomingCapturedFrame(CreateFrame(1, kFrameWidth, kFrameHeight));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      kDefaultFramerate);

  // Trigger reconfigure encoder (without resetting the entire instance).
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.simulcast_layers[0].max_framerate = kFramerate;
  video_encoder_config.max_bitrate_bps = kTargetBitrate.bps();
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Detector should be updated with fps limit from codec config.
  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      kFramerate);

  // Trigger overuse, max framerate should be reduced.
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kFramerate;
  stats_proxy_->SetMockStats(stats);
  video_stream_encoder_->TriggerCpuOveruse();
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  int adapted_framerate =
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate();
  EXPECT_LT(adapted_framerate, kFramerate);

  // Trigger underuse, max framerate should go back to codec configured fps.
  // Set extra low fps, to make sure it's actually reset, not just incremented.
  stats = stats_proxy_->GetStats();
  stats.input_frame_rate = adapted_framerate / 2;
  stats_proxy_->SetMockStats(stats);
  video_stream_encoder_->TriggerCpuUnderuse();
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      kFramerate);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       OveruseDetectorUpdatedRespectsFramerateAfterUnderuse) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  const int kLowFramerate = 15;
  const int kHighFramerate = 25;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);

  // Trigger initial configuration.
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.simulcast_layers[0].max_framerate = kLowFramerate;
  video_encoder_config.max_bitrate_bps = kTargetBitrate.bps();
  source.IncomingCapturedFrame(CreateFrame(1, kFrameWidth, kFrameHeight));
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      kLowFramerate);

  // Trigger overuse, max framerate should be reduced.
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kLowFramerate;
  stats_proxy_->SetMockStats(stats);
  video_stream_encoder_->TriggerCpuOveruse();
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  int adapted_framerate =
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate();
  EXPECT_LT(adapted_framerate, kLowFramerate);

  // Reconfigure the encoder with a new (higher max framerate), max fps should
  // still respect the adaptation.
  video_encoder_config.simulcast_layers[0].max_framerate = kHighFramerate;
  source.IncomingCapturedFrame(CreateFrame(2, kFrameWidth, kFrameHeight));
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      adapted_framerate);

  // Trigger underuse, max framerate should go back to codec configured fps.
  stats = stats_proxy_->GetStats();
  stats.input_frame_rate = adapted_framerate;
  stats_proxy_->SetMockStats(stats);
  video_stream_encoder_->TriggerCpuUnderuse();
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      kHighFramerate);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       OveruseDetectorUpdatedOnDegradationPreferenceChange) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  const int kFramerate = 24;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_RESOLUTION);

  // Trigger initial configuration.
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.simulcast_layers[0].max_framerate = kFramerate;
  video_encoder_config.max_bitrate_bps = kTargetBitrate.bps();
  source.IncomingCapturedFrame(CreateFrame(1, kFrameWidth, kFrameHeight));
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      kFramerate);

  // Trigger overuse, max framerate should be reduced.
  VideoSendStream::Stats stats = stats_proxy_->GetStats();
  stats.input_frame_rate = kFramerate;
  stats_proxy_->SetMockStats(stats);
  video_stream_encoder_->TriggerCpuOveruse();
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  int adapted_framerate =
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate();
  EXPECT_LT(adapted_framerate, kFramerate);

  // Change degradation preference to not enable framerate scaling. Target
  // framerate should be changed to codec defined limit.
  video_stream_encoder_->SetSourceAndWaitForFramerateUpdated(
      &source, DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_EQ(
      video_stream_encoder_->overuse_detector_proxy_->GetLastTargetFramerate(),
      kFramerate);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropsFramesAndScalesWhenBitrateIsTooLow) {
  const int kTooLowBitrateForFrameSizeBps = 10000;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps),
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps), 0, 0, 0);
  const int kWidth = 640;
  const int kHeight = 360;

  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));

  // Expect to drop this frame, the wait should time out.
  ExpectDroppedFrame();

  // Expect the sink_wants to specify a scaled frame.
  EXPECT_THAT(
      WaitUntil([&] { return video_source_.sink_wants().max_pixel_count; },
                Lt(kWidth * kHeight)),
      IsRtcOk());

  int last_pixel_count = video_source_.sink_wants().max_pixel_count;

  // Next frame is scaled.
  video_source_.IncomingCapturedFrame(
      CreateFrame(2, kWidth * 3 / 4, kHeight * 3 / 4));

  // Expect to drop this frame, the wait should time out.
  ExpectDroppedFrame();

  EXPECT_THAT(
      WaitUntil([&] { return video_source_.sink_wants().max_pixel_count; },
                Lt(last_pixel_count)),
      IsRtcOk());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       NumberOfDroppedFramesLimitedWhenBitrateIsTooLow) {
  const int kTooLowBitrateForFrameSizeBps = 10000;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps),
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps), 0, 0, 0);
  const int kWidth = 640;
  const int kHeight = 360;

  // We expect the n initial frames to get dropped.
  int i;
  for (i = 1; i <= kMaxInitialFramedrop; ++i) {
    video_source_.IncomingCapturedFrame(CreateFrame(i, kWidth, kHeight));
    ExpectDroppedFrame();
  }
  // The n+1th frame should not be dropped, even though it's size is too large.
  video_source_.IncomingCapturedFrame(CreateFrame(i, kWidth, kHeight));
  WaitForEncodedFrame(i);

  // Expect the sink_wants to specify a scaled frame.
  EXPECT_LT(video_source_.sink_wants().max_pixel_count, kWidth * kHeight);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       InitialFrameDropOffWithMaintainResolutionPreference) {
  const int kWidth = 640;
  const int kHeight = 360;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kLowTargetBitrate, kLowTargetBitrate, 0, 0, 0);

  // Set degradation preference.
  video_stream_encoder_->SetSource(&video_source_,
                                   DegradationPreference::MAINTAIN_RESOLUTION);

  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  // Frame should not be dropped, even if it's too large.
  WaitForEncodedFrame(1);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, InitialFrameDropOffWhenEncoderDisabledScaling) {
  const int kWidth = 640;
  const int kHeight = 360;
  fake_encoder_.SetQualityScaling(false);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  // Make format different, to force recreation of encoder.
  video_encoder_config.video_format.parameters["foo"] = "foo";
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kLowTargetBitrate, kLowTargetBitrate, 0, 0, 0);

  // Force quality scaler reconfiguration by resetting the source.
  video_stream_encoder_->SetSource(&video_source_,
                                   DegradationPreference::BALANCED);

  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  // Frame should not be dropped, even if it's too large.
  WaitForEncodedFrame(1);

  video_stream_encoder_->Stop();
  fake_encoder_.SetQualityScaling(true);
}

TEST_F(VideoStreamEncoderTest, InitialFrameDropActivatesWhenBweDrops) {
  auto field_trials = SetFieldTrial(
      "WebRTC-Video-QualityScalerSettings",
      "initial_bitrate_interval_ms:1000,initial_bitrate_factor:0.2");
  // Reset encoder for field trials to take effect.
  ConfigureEncoder(video_encoder_config_.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
                   /* num_cores= */ 1, &field_trials);

  const int kNotTooLowBitrateForFrameSizeBps = kTargetBitrate.bps() * 0.2;
  const int kTooLowBitrateForFrameSizeBps = kTargetBitrate.bps() * 0.19;
  const int kWidth = 640;
  const int kHeight = 360;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(1);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kNotTooLowBitrateForFrameSizeBps),
      DataRate::BitsPerSec(kNotTooLowBitrateForFrameSizeBps), 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(2);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps),
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps), 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  // Expect to drop this frame, the wait should time out.
  ExpectDroppedFrame();

  // Expect the sink_wants to specify a scaled frame.
  EXPECT_THAT(
      WaitUntil([&] { return video_source_.sink_wants().max_pixel_count; },
                Lt(kWidth * kHeight)),
      IsRtcOk());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       InitialFrameDropNotReactivatedWhenBweDropsWhenScalingDisabled) {
  auto field_trials = SetFieldTrial(
      "WebRTC-Video-QualityScalerSettings",
      "initial_bitrate_interval_ms:1000,initial_bitrate_factor:0.2");
  fake_encoder_.SetQualityScaling(false);
  ConfigureEncoder(video_encoder_config_.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
                   /* num_cores= */ 1, &field_trials);

  const int kNotTooLowBitrateForFrameSizeBps = kTargetBitrate.bps() * 0.2;
  const int kTooLowBitrateForFrameSizeBps = kTargetBitrate.bps() * 0.19;
  const int kWidth = 640;
  const int kHeight = 360;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(1);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kNotTooLowBitrateForFrameSizeBps),
      DataRate::BitsPerSec(kNotTooLowBitrateForFrameSizeBps), 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(2);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps),
      DataRate::BitsPerSec(kTooLowBitrateForFrameSizeBps), 0, 0, 0);
  video_source_.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  // Not dropped since quality scaling is disabled.
  WaitForEncodedFrame(3);

  // Expect the sink_wants to specify a scaled frame.
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_THAT(video_source_.sink_wants(), ResolutionMax());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, InitialFrameDropAccountsForResolutionScaling) {
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(PayloadStringToCodecType("VP8"), 1,
                                 &video_encoder_config);
  video_encoder_config.video_stream_factory = nullptr;
  for (auto& layer : video_encoder_config.simulcast_layers) {
    layer.num_temporal_layers = 1;
    layer.max_framerate = kDefaultFramerate;
  }
  video_encoder_config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;

  video_encoder_config.simulcast_layers[0].active = true;
  video_encoder_config.simulcast_layers[0].scale_resolution_down_by = 4;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Bitrate is not enough for 720p.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::KilobitsPerSec(30), DataRate::KilobitsPerSec(30), 0, 0, 0);

  // Pass 720p frame. Resolution scaling factor is set to 4 which means that
  // the target encode resolution is 180p. The default initial frame dropping
  // should not be active for 180p no matter of available bitrate.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, InitialFrameDropActivatesWhenLayersChange) {
  // Set simulcast.
  ResetEncoder("VP8", 3, 1, 1, false);
  fake_encoder_.SetQualityScaling(true);
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(400),
      /*link_allocation=*/DataRate::KilobitsPerSec(400),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(1);

  // Trigger QVGA "singlecast"
  // Update the config.
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(PayloadStringToCodecType("VP8"), 3,
                                 &video_encoder_config);
  video_encoder_config.video_stream_factory = nullptr;
  for (auto& layer : video_encoder_config.simulcast_layers) {
    layer.num_temporal_layers = 1;
    layer.max_framerate = kDefaultFramerate;
  }
  video_encoder_config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;

  video_encoder_config.simulcast_layers[0].active = true;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = false;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(2);

  // Trigger HD "singlecast"
  video_encoder_config.simulcast_layers[0].active = false;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = true;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_source_.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  // Frame should be dropped because of initial frame drop.
  ExpectDroppedFrame();

  // Expect the sink_wants to specify a scaled frame.
  EXPECT_THAT(
      WaitUntil([&] { return video_source_.sink_wants().max_pixel_count; },
                Lt(kWidth * kHeight)),
      IsRtcOk());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, InitialFrameDropActivatesWhenSVCLayersChange) {
  // Set simulcast.
  ResetEncoder("VP9", 1, 1, 3, false);
  fake_encoder_.SetQualityScaling(true);
  const int kWidth = 1280;
  const int kHeight = 720;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(400),

      /*link_allocation=*/DataRate::KilobitsPerSec(400),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  video_source_.IncomingCapturedFrame(CreateFrame(1, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(1);

  // Trigger QVGA "singlecast"
  // Update the config.
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(PayloadStringToCodecType("VP9"), 1,
                                 &video_encoder_config);
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 3;
  // Since only one layer is active - automatic resize should be enabled.
  vp9_settings.automaticResizeOn = true;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  video_encoder_config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  // Currently simulcast layers `active` flags are used to inidicate
  // which SVC layers are active.
  video_encoder_config.simulcast_layers.resize(3);

  video_encoder_config.simulcast_layers[0].active = true;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = false;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_source_.IncomingCapturedFrame(CreateFrame(2, kWidth, kHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(2);

  // Trigger HD "singlecast"
  video_encoder_config.simulcast_layers[0].active = false;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = true;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_source_.IncomingCapturedFrame(CreateFrame(3, kWidth, kHeight));
  // Frame should be dropped because of initial frame drop.
  ExpectDroppedFrame();

  // Expect the sink_wants to specify a scaled frame.
  EXPECT_THAT(
      WaitUntil([&] { return video_source_.sink_wants().max_pixel_count; },
                Lt(kWidth * kHeight)),
      IsRtcOk());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderMaxAndMinBitratesUsedIfMiddleStreamActive) {
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits270p(
      480 * 270, 34 * 1000, 12 * 1000, 1234 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits360p(
      640 * 360, 43 * 1000, 21 * 1000, 2345 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits720p(
      1280 * 720, 54 * 1000, 31 * 1000, 2500 * 1000);
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderLimits270p, kEncoderLimits360p, kEncoderLimits720p});

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(PayloadStringToCodecType("VP9"), 1,
                                 &video_encoder_config);
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 3;
  // Since only one layer is active - automatic resize should be enabled.
  vp9_settings.automaticResizeOn = true;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  video_encoder_config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  // Simulcast layers are used to indicate which spatial layers are active.
  video_encoder_config.simulcast_layers.resize(3);
  video_encoder_config.simulcast_layers[0].active = false;
  video_encoder_config.simulcast_layers[1].active = true;
  video_encoder_config.simulcast_layers[2].active = false;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  // The encoder bitrate limits for 360p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, 1);
  EXPECT_EQ(fake_encoder_.config().codecType, VideoCodecType::kVideoCodecVP9);
  EXPECT_EQ(fake_encoder_.config().VP9().numberOfSpatialLayers, 2);
  EXPECT_TRUE(fake_encoder_.config().spatialLayers[0].active);
  EXPECT_EQ(640, fake_encoder_.config().spatialLayers[0].width);
  EXPECT_EQ(360, fake_encoder_.config().spatialLayers[0].height);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.min_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits360p.max_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].maxBitrate * 1000);

  // The encoder bitrate limits for 270p should be used.
  video_source_.IncomingCapturedFrame(CreateFrame(2, 960, 540));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, 1);
  EXPECT_EQ(fake_encoder_.config().codecType, VideoCodecType::kVideoCodecVP9);
  EXPECT_EQ(fake_encoder_.config().VP9().numberOfSpatialLayers, 2);
  EXPECT_TRUE(fake_encoder_.config().spatialLayers[0].active);
  EXPECT_EQ(480, fake_encoder_.config().spatialLayers[0].width);
  EXPECT_EQ(270, fake_encoder_.config().spatialLayers[0].height);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.min_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kEncoderLimits270p.max_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       DefaultMaxAndMinBitratesUsedIfMiddleStreamActive) {
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(PayloadStringToCodecType("VP9"), 1,
                                 &video_encoder_config);
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 3;
  // Since only one layer is active - automatic resize should be enabled.
  vp9_settings.automaticResizeOn = true;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  video_encoder_config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  // Simulcast layers are used to indicate which spatial layers are active.
  video_encoder_config.simulcast_layers.resize(3);
  video_encoder_config.simulcast_layers[0].active = false;
  video_encoder_config.simulcast_layers[1].active = true;
  video_encoder_config.simulcast_layers[2].active = false;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  // The default bitrate limits for 360p should be used.
  const std::optional<VideoEncoder::ResolutionBitrateLimits> kLimits360p =
      EncoderInfoSettings::GetDefaultSinglecastBitrateLimitsForResolution(
          kVideoCodecVP9, 640 * 360);
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, 1);
  EXPECT_EQ(fake_encoder_.config().codecType, VideoCodecType::kVideoCodecVP9);
  EXPECT_EQ(fake_encoder_.config().VP9().numberOfSpatialLayers, 2);
  EXPECT_TRUE(fake_encoder_.config().spatialLayers[0].active);
  EXPECT_EQ(640, fake_encoder_.config().spatialLayers[0].width);
  EXPECT_EQ(360, fake_encoder_.config().spatialLayers[0].height);
  EXPECT_EQ(static_cast<uint32_t>(kLimits360p->min_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kLimits360p->max_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].maxBitrate * 1000);

  // The default bitrate limits for 270p should be used.
  const std::optional<VideoEncoder::ResolutionBitrateLimits> kLimits270p =
      EncoderInfoSettings::GetDefaultSinglecastBitrateLimitsForResolution(
          kVideoCodecVP9, 480 * 270);
  video_source_.IncomingCapturedFrame(CreateFrame(2, 960, 540));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, 1);
  EXPECT_EQ(fake_encoder_.config().codecType, VideoCodecType::kVideoCodecVP9);
  EXPECT_EQ(fake_encoder_.config().VP9().numberOfSpatialLayers, 2);
  EXPECT_TRUE(fake_encoder_.config().spatialLayers[0].active);
  EXPECT_EQ(480, fake_encoder_.config().spatialLayers[0].width);
  EXPECT_EQ(270, fake_encoder_.config().spatialLayers[0].height);
  EXPECT_EQ(static_cast<uint32_t>(kLimits270p->min_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].minBitrate * 1000);
  EXPECT_EQ(static_cast<uint32_t>(kLimits270p->max_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SinglecastBitrateLimitsNotUsedForOneStream) {
  ResetEncoder("VP9", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1, /*screenshare=*/false);

  // The default singlecast bitrate limits for 720p should not be used.
  const std::optional<VideoEncoder::ResolutionBitrateLimits> kLimits720p =
      EncoderInfoSettings::GetDefaultSinglecastBitrateLimitsForResolution(
          kVideoCodecVP9, 1280 * 720);
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, 1);
  EXPECT_EQ(fake_encoder_.config().codecType, VideoCodecType::kVideoCodecVP9);
  EXPECT_EQ(fake_encoder_.config().VP9().numberOfSpatialLayers, 1);
  EXPECT_TRUE(fake_encoder_.config().spatialLayers[0].active);
  EXPECT_EQ(1280, fake_encoder_.config().spatialLayers[0].width);
  EXPECT_EQ(720, fake_encoder_.config().spatialLayers[0].height);
  EXPECT_NE(static_cast<uint32_t>(kLimits720p->max_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       EncoderMaxAndMinBitratesNotUsedIfLowestStreamActive) {
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits180p(
      320 * 180, 34 * 1000, 12 * 1000, 1234 * 1000);
  const VideoEncoder::ResolutionBitrateLimits kEncoderLimits720p(
      1280 * 720, 54 * 1000, 31 * 1000, 2500 * 1000);
  fake_encoder_.SetResolutionBitrateLimits(
      {kEncoderLimits180p, kEncoderLimits720p});

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(PayloadStringToCodecType("VP9"), 1,
                                 &video_encoder_config);
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 3;
  // Since only one layer is active - automatic resize should be enabled.
  vp9_settings.automaticResizeOn = true;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  video_encoder_config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  // Simulcast layers are used to indicate which spatial layers are active.
  video_encoder_config.simulcast_layers.resize(3);
  video_encoder_config.simulcast_layers[0].active = true;
  video_encoder_config.simulcast_layers[1].active = false;
  video_encoder_config.simulcast_layers[2].active = false;

  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  // Limits not applied on lowest stream, limits for 180p should not be used.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(fake_encoder_.config().numberOfSimulcastStreams, 1);
  EXPECT_EQ(fake_encoder_.config().codecType, VideoCodecType::kVideoCodecVP9);
  EXPECT_EQ(fake_encoder_.config().VP9().numberOfSpatialLayers, 3);
  EXPECT_TRUE(fake_encoder_.config().spatialLayers[0].active);
  EXPECT_EQ(320, fake_encoder_.config().spatialLayers[0].width);
  EXPECT_EQ(180, fake_encoder_.config().spatialLayers[0].height);
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits180p.min_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].minBitrate * 1000);
  EXPECT_NE(static_cast<uint32_t>(kEncoderLimits180p.max_bitrate_bps),
            fake_encoder_.config().spatialLayers[0].maxBitrate * 1000);

  video_stream_encoder_->Stop();
}

enum class FrameResolutionChangeMethod {
  MODIFY_SOURCE,
  MODIFY_SCALE_RESOLUTION_DOWN_TO,
  MODIFY_SCALE_RESOLUTION_DOWN_BY,
};
class VideoStreamEncoderInitialFrameDropperTest
    : public VideoStreamEncoderTest,
      public ::testing::WithParamInterface<FrameResolutionChangeMethod> {
 public:
  VideoStreamEncoderInitialFrameDropperTest()
      : VideoStreamEncoderTest(), frame_resolution_change_method_(GetParam()) {}

  void SetUp() override {
    VideoStreamEncoderTest::SetUp();
    switch (frame_resolution_change_method_) {
      case FrameResolutionChangeMethod::MODIFY_SOURCE:
        break;
      case FrameResolutionChangeMethod::MODIFY_SCALE_RESOLUTION_DOWN_TO:
        video_encoder_config_.video_stream_factory = nullptr;
        captureWidth = kWidth;
        captureHeight = kHeight;
        break;
      case FrameResolutionChangeMethod::MODIFY_SCALE_RESOLUTION_DOWN_BY:
        captureWidth = kWidth;
        captureHeight = kHeight;
        break;
    }
  }

  void SetEncoderFrameSize(int width, int height) {
    switch (frame_resolution_change_method_) {
      case FrameResolutionChangeMethod::MODIFY_SOURCE:
        captureWidth = width;
        captureHeight = height;
        break;
      case FrameResolutionChangeMethod::MODIFY_SCALE_RESOLUTION_DOWN_TO:
        ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
        video_encoder_config_.simulcast_layers[0]
            .scale_resolution_down_to.emplace(
                Resolution({.width = width, .height = height}));
        video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                                kMaxPayloadLength);
        break;
      case FrameResolutionChangeMethod::MODIFY_SCALE_RESOLUTION_DOWN_BY:
        ASSERT_THAT(video_encoder_config_.simulcast_layers, SizeIs(1));
        double scale_height =
            static_cast<double>(kHeight) / static_cast<double>(height);
        double scale_width =
            static_cast<double>(kWidth) / static_cast<double>(width);
        video_encoder_config_.simulcast_layers[0].scale_resolution_down_by =
            std::max(scale_width, scale_height);
        video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                                kMaxPayloadLength);
        break;
    }
  }

  const int kWidth = 640;
  const int kHeight = 360;

  int captureWidth = 0;
  int captureHeight = 0;

 protected:
  const FrameResolutionChangeMethod frame_resolution_change_method_;
};

INSTANTIATE_TEST_SUITE_P(
    VideoStreamEncoderInitialFrameDropperTest,
    VideoStreamEncoderInitialFrameDropperTest,
    ::testing::Values(
        FrameResolutionChangeMethod::MODIFY_SOURCE,
        FrameResolutionChangeMethod::MODIFY_SCALE_RESOLUTION_DOWN_TO,
        FrameResolutionChangeMethod::MODIFY_SCALE_RESOLUTION_DOWN_BY));

TEST_P(VideoStreamEncoderInitialFrameDropperTest,
       InitialFrameDropActivatesWhenResolutionIncreases) {
  SetEncoderFrameSize(kWidth / 2, kHeight / 2);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, captureWidth, captureHeight));
  // Frame should not be dropped.
  WaitForEncodedFrame(1);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kLowTargetBitrate, kLowTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(2, captureWidth, captureHeight));
  // Frame should not be dropped, bitrate not too low for frame.
  WaitForEncodedFrame(2);

  // Incoming resolution increases.
  SetEncoderFrameSize(kWidth, kHeight);

  video_source_.IncomingCapturedFrame(
      CreateFrame(3, captureWidth, captureHeight));
  // Expect to drop this frame, bitrate too low for frame.
  ExpectDroppedFrame();

  // Expect the sink_wants to specify a scaled frame.
  EXPECT_THAT(
      WaitUntil([&] { return video_source_.sink_wants().max_pixel_count; },
                Lt(kWidth * kHeight)),
      IsRtcOk());
  video_stream_encoder_->Stop();
}

TEST_P(VideoStreamEncoderInitialFrameDropperTest,
       InitialFrameDropIsNotReactivatedWhenAdaptingUp) {
  const int kWidth = 640;
  const int kHeight = 360;
  // So that quality scaling doesn't happen by itself.
  fake_encoder_.SetQp(kQpHigh);

  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);

  int timestamp = 1;

  // By using the `scale_resolution_down_to` API, ReconfigureEncoder() gets
  // triggered from VideoStreamEncoder::OnVideoSourceRestrictionsUpdated().
  SetEncoderFrameSize(kWidth, kHeight);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  source.IncomingCapturedFrame(
      CreateFrame(timestamp, captureWidth, captureHeight));
  WaitForEncodedFrame(timestamp);
  timestamp += 9000;
  // Long pause to disable all first BWE drop logic.
  AdvanceTime(TimeDelta::Millis(1000));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kLowTargetBitrate, kLowTargetBitrate, 0, 0, 0);
  source.IncomingCapturedFrame(
      CreateFrame(timestamp, captureWidth, captureHeight));
  // Not dropped frame, as initial frame drop is disabled by now.
  WaitForEncodedFrame(timestamp);
  timestamp += 9000;
  AdvanceTime(TimeDelta::Millis(100));

  // Quality adaptation down.
  video_stream_encoder_->TriggerQualityLow();

  // Adaptation has an effect.
  EXPECT_THAT(WaitUntil([&] { return source.sink_wants().max_pixel_count; },
                        Lt(kWidth * kHeight)),
              IsRtcOk());

  // Frame isn't dropped as initial frame dropper is disabled.
  source.IncomingCapturedFrame(
      CreateFrame(timestamp, captureWidth, captureHeight));
  WaitForEncodedFrame(timestamp);
  timestamp += 9000;
  AdvanceTime(TimeDelta::Millis(100));

  // Quality adaptation up.
  video_stream_encoder_->TriggerQualityHigh();

  // Adaptation has an effect.
  EXPECT_THAT(WaitUntil([&] { return source.sink_wants().max_pixel_count; },
                        Gt(kWidth * kHeight)),
              IsRtcOk());

  source.IncomingCapturedFrame(
      CreateFrame(timestamp, captureWidth, captureHeight));
  // Frame should not be dropped, as initial framedropper is off.
  WaitForEncodedFrame(timestamp);

  video_stream_encoder_->Stop();
}

TEST_P(VideoStreamEncoderInitialFrameDropperTest,
       FrameDroppedWhenResolutionIncreasesAndLinkAllocationIsLow) {
  const int kMinStartBps360p = 222000;
  fake_encoder_.SetResolutionBitrateLimits(
      {VideoEncoder::ResolutionBitrateLimits(320 * 180, 0, 30000, 400000),
       VideoEncoder::ResolutionBitrateLimits(640 * 360, kMinStartBps360p, 30000,
                                             800000)});

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kMinStartBps360p - 1),  // target_bitrate
      DataRate::BitsPerSec(kMinStartBps360p - 1),  // link_allocation
      0, 0, 0);

  SetEncoderFrameSize(kWidth / 2, kHeight / 2);

  // Frame should not be dropped, bitrate not too low for frame.
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, captureWidth, captureHeight));
  WaitForEncodedFrame(1);

  // Incoming resolution increases, initial frame drop activates.
  // Frame should be dropped, link allocation too low for frame.
  SetEncoderFrameSize(kWidth, kHeight);

  video_source_.IncomingCapturedFrame(
      CreateFrame(2, captureWidth, captureHeight));
  ExpectDroppedFrame();

  // Expect sink_wants to specify a scaled frame.
  EXPECT_THAT(
      WaitUntil([&] { return video_source_.sink_wants().max_pixel_count; },
                Lt(640 * 360)),
      IsRtcOk());
  video_stream_encoder_->Stop();
}

TEST_P(VideoStreamEncoderInitialFrameDropperTest,
       FrameNotDroppedWhenResolutionIncreasesAndLinkAllocationIsHigh) {
  const int kMinStartBps360p = 222000;
  fake_encoder_.SetResolutionBitrateLimits(
      {VideoEncoder::ResolutionBitrateLimits(320 * 180, 0, 30000, 400000),
       VideoEncoder::ResolutionBitrateLimits(640 * 360, kMinStartBps360p, 30000,
                                             800000)});

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(kMinStartBps360p - 1),  // target_bitrate
      DataRate::BitsPerSec(kMinStartBps360p),      // link_allocation
      0, 0, 0);

  const int kWidth = 640;
  const int kHeight = 360;

  SetEncoderFrameSize(kWidth / 2, kHeight / 2);

  // Frame should not be dropped, bitrate not too low for frame.
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, captureWidth, captureHeight));
  WaitForEncodedFrame(1);

  // Incoming resolution increases, initial frame drop activates.
  SetEncoderFrameSize(kWidth, kHeight);

  // Frame should be dropped, link allocation not too low for frame.
  video_source_.IncomingCapturedFrame(
      CreateFrame(2, captureWidth, captureHeight));
  WaitForEncodedFrame(2);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       QualityScalerAdaptationsRemovedWhenQualityScalingDisabled) {
  // Reset encoder for new configuration to take effect.
  auto field_trials = SetFieldTrial("WebRTC-Video-QualityScaling", "Disabled");
  ConfigureEncoder(video_encoder_config_.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
                   /* num_cores= */ 1, &field_trials);

  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  fake_encoder_.SetQp(kQpHigh + 1);
  const int kWidth = 1280;
  const int kHeight = 720;
  const int64_t kFrameInterval100Ms = 100;
  int64_t timestamp_ms = kFrameIntervalMs;
  for (size_t i = 1; i <= 100; i++) {
    timestamp_ms += kFrameInterval100Ms;
    source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
    WaitForEncodedFrame(timestamp_ms);
  }
  // Wait for QualityScaler, which will wait for 2000*2.5 ms until checking QP
  // for the first time.
  // TODO(eshr): We should avoid these waits by using threads with simulated
  // time.
  EXPECT_TRUE(
      WaitUntil([&] { return stats_proxy_->GetStats().bw_limited_resolution; },
                {.timeout = TimeDelta::Millis(2000 * 2.5 * 2)}));
  timestamp_ms += kFrameInterval100Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_THAT(source.sink_wants(), WantsMaxPixels(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);

  // Disable Quality scaling by turning off scaler on the encoder and
  // reconfiguring.
  fake_encoder_.SetQualityScaling(false);
  video_stream_encoder_->ConfigureEncoder(video_encoder_config_.Copy(),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  AdvanceTime(TimeDelta::Zero());
  // Since we turned off the quality scaler, the adaptations made by it are
  // removed.
  EXPECT_THAT(source.sink_wants(), ResolutionMax());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ResolutionNotAdaptedForTooSmallFrame_MaintainFramerateMode) {
  const int kTooSmallWidth = 10;
  const int kTooSmallHeight = 10;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable MAINTAIN_FRAMERATE preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);

  // Trigger adapt down, too small frame, expect no change.
  source.IncomingCapturedFrame(CreateFrame(1, kTooSmallWidth, kTooSmallHeight));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerCpuOveruse();
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ResolutionNotAdaptedForTooSmallFrame_BalancedMode) {
  const int kTooSmallWidth = 10;
  const int kTooSmallHeight = 10;
  const int kFpsLimit = 7;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);

  // Trigger adapt down, expect limited framerate.
  source.IncomingCapturedFrame(CreateFrame(1, kTooSmallWidth, kTooSmallHeight));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_THAT(source.sink_wants(), FpsMatchesResolutionMax(Eq(kFpsLimit)));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, too small frame, expect no change.
  source.IncomingCapturedFrame(CreateFrame(2, kTooSmallWidth, kTooSmallHeight));
  WaitForEncodedFrame(2);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_THAT(source.sink_wants(), FpsMatchesResolutionMax(Eq(kFpsLimit)));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, FailingInitEncodeDoesntCauseCrash) {
  fake_encoder_.ForceInitEncodeFailure(true);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  ResetEncoder("VP8", 2, 1, 1, false);
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, kFrameWidth, kFrameHeight));
  ExpectDroppedFrame();
  video_stream_encoder_->Stop();
}

// TODO(sprang): Extend this with fps throttling and any "balanced" extensions.
TEST_F(VideoStreamEncoderTest,
       AdaptsResolutionOnOveruse_MaintainFramerateMode) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  // Enabled default VideoAdapter downscaling. First step is 3/4, not 3/5 as
  // requested by
  // VideoStreamEncoder::VideoSourceProxy::RequestResolutionLowerThan().
  video_source_.set_adaptation_enabled(true);

  video_source_.IncomingCapturedFrame(
      CreateFrame(1 * kFrameIntervalMs, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(kFrameWidth, kFrameHeight);

  // Trigger CPU overuse, downscale by 3/4.
  video_stream_encoder_->TriggerCpuOveruse();
  video_source_.IncomingCapturedFrame(
      CreateFrame(2 * kFrameIntervalMs, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame((kFrameWidth * 3) / 4, (kFrameHeight * 3) / 4);

  // Trigger CPU normal use, return to original resolution.
  video_stream_encoder_->TriggerCpuUnderuse();
  video_source_.IncomingCapturedFrame(
      CreateFrame(3 * kFrameIntervalMs, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(kFrameWidth, kFrameHeight);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptsFramerateOnOveruse_MaintainResolutionMode) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_stream_encoder_->SetSource(&video_source_,
                                   DegradationPreference::MAINTAIN_RESOLUTION);
  video_source_.set_adaptation_enabled(true);

  int64_t timestamp_ms = CurrentTimeMs();

  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(timestamp_ms);

  // Try to trigger overuse. No fps estimate available => no effect.
  video_stream_encoder_->TriggerCpuOveruse();

  // Insert frames for one second to get a stable estimate.
  for (int i = 0; i < max_framerate_; ++i) {
    timestamp_ms += kFrameIntervalMs;
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    WaitForEncodedFrame(timestamp_ms);
  }

  // Trigger CPU overuse, reduce framerate by 2/3.
  video_stream_encoder_->TriggerCpuOveruse();
  int num_frames_dropped = 0;
  for (int i = 0; i < max_framerate_; ++i) {
    timestamp_ms += kFrameIntervalMs;
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    if (!WaitForFrame(kFrameTimeout)) {
      ++num_frames_dropped;
    } else {
      sink_.CheckLastFrameSizeMatches(kFrameWidth, kFrameHeight);
    }
  }

  // Add some slack to account for frames dropped by the frame dropper.
  const int kErrorMargin = 1;
  EXPECT_NEAR(num_frames_dropped, max_framerate_ - (max_framerate_ * 2 / 3),
              kErrorMargin);

  // Trigger CPU overuse, reduce framerate by 2/3 again.
  video_stream_encoder_->TriggerCpuOveruse();
  num_frames_dropped = 0;
  for (int i = 0; i <= max_framerate_; ++i) {
    timestamp_ms += kFrameIntervalMs;
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    if (!WaitForFrame(kFrameTimeout)) {
      ++num_frames_dropped;
    } else {
      sink_.CheckLastFrameSizeMatches(kFrameWidth, kFrameHeight);
    }
  }
  EXPECT_NEAR(num_frames_dropped, max_framerate_ - (max_framerate_ * 4 / 9),
              kErrorMargin);

  // Go back up one step.
  video_stream_encoder_->TriggerCpuUnderuse();
  num_frames_dropped = 0;
  for (int i = 0; i < max_framerate_; ++i) {
    timestamp_ms += kFrameIntervalMs;
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    if (!WaitForFrame(kFrameTimeout)) {
      ++num_frames_dropped;
    } else {
      sink_.CheckLastFrameSizeMatches(kFrameWidth, kFrameHeight);
    }
  }
  EXPECT_NEAR(num_frames_dropped, max_framerate_ - (max_framerate_ * 2 / 3),
              kErrorMargin);

  // Go back up to original mode.
  video_stream_encoder_->TriggerCpuUnderuse();
  num_frames_dropped = 0;
  for (int i = 0; i < max_framerate_; ++i) {
    timestamp_ms += kFrameIntervalMs;
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    if (!WaitForFrame(kFrameTimeout)) {
      ++num_frames_dropped;
    } else {
      sink_.CheckLastFrameSizeMatches(kFrameWidth, kFrameHeight);
    }
  }
  EXPECT_NEAR(num_frames_dropped, 0, kErrorMargin);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DoesntAdaptDownPastMinFramerate) {
  const int kFramerateFps = 5;
  const int kFrameInterval5FpsInMs = kNumMillisecsPerSec / kFramerateFps;
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  // Reconfigure encoder with two temporal layers and screensharing, which will
  // disable frame dropping and make testing easier.
  ResetEncoder("VP8", 1, 2, 1, true);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_stream_encoder_->SetSource(&video_source_,
                                   DegradationPreference::MAINTAIN_RESOLUTION);
  video_source_.set_adaptation_enabled(true);

  int64_t timestamp_ms = CurrentTimeMs();

  // Trigger overuse as much as we can.
  VideoSinkWants last_wants;
  do {
    last_wants = video_source_.sink_wants();

    // Insert frames to get a new fps estimate...
    for (int j = 0; j < kFramerateFps; ++j) {
      video_source_.IncomingCapturedFrame(
          CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
      if (video_source_.last_sent_width()) {
        sink_.WaitForEncodedFrame(timestamp_ms);
      }
      timestamp_ms += kFrameInterval5FpsInMs;
      AdvanceTime(TimeDelta::Millis(kFrameInterval5FpsInMs));
    }
    // ...and then try to adapt again.
    video_stream_encoder_->TriggerCpuOveruse();
  } while (video_source_.sink_wants().max_framerate_fps <
           last_wants.max_framerate_fps);

  EXPECT_THAT(video_source_.sink_wants(),
              FpsMatchesResolutionMax(Eq(kMinFramerateFps)));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptsResolutionAndFramerateForLowQuality_BalancedMode) {
  const int kWidth = 1280;
  const int kHeight = 720;
  const int64_t kFrameInterval150Ms = 150;
  int64_t timestamp_ms = kFrameInterval150Ms;
  ASSERT_EQ(video_encoder_config_.simulcast_layers.size(), 1u);
  video_encoder_config_.simulcast_layers[0].width = kWidth;
  video_encoder_config_.simulcast_layers[0].height = kHeight;
  video_encoder_config_.simulcast_layers[0].max_framerate = kDefaultFramerate;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (960x540@30fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsMaxResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (640x360@30fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionLt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect reduced fps (640x360@15fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (480x270@15fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionLt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Restrict bitrate, trigger adapt down, expect reduced fps (480x270@10fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(5, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect scaled down resolution (320x180@10fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionLt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(6, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, expect reduced fps (320x180@7fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  VideoSinkWants last_wants = source.sink_wants();
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(7, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt down, min resolution reached, expect no change.
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionEqTo(last_wants));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(7, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect increased fps (320x180@10fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(8, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled resolution (480x270@10fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionGt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(9, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Increase bitrate, trigger adapt up, expect increased fps (480x270@15fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(10, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled resolution (640x360@15fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionGt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(11, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect increased fps (640x360@30fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsMax());
  EXPECT_EQ(source.sink_wants().max_pixel_count,
            source.last_wants().max_pixel_count);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(12, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect upscaled resolution (960x540@30fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionGt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(13, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no restriction (1280x720fps@30fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionGt(source.last_wants()));
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_EQ(14, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no change.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_EQ(14, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, AdaptWithTwoReasonsAndDifferentOrder_Framerate) {
  const int kWidth = 1280;
  const int kHeight = 720;
  const int64_t kFrameInterval150Ms = 150;
  int64_t timestamp_ms = kFrameInterval150Ms;
  ASSERT_EQ(video_encoder_config_.simulcast_layers.size(), 1u);
  video_encoder_config_.simulcast_layers[0].width = kWidth;
  video_encoder_config_.simulcast_layers[0].height = kHeight;
  video_encoder_config_.simulcast_layers[0].max_framerate = kDefaultFramerate;
  ConfigureEncoder(video_encoder_config_.Copy());
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt down, expect scaled down resolution (960x540@30fps).
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(),
              FpsMaxResolutionMatches(Lt(kWidth * kHeight)));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt down, expect scaled down resolution (640x360@30fps).
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionLt(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt down, expect reduced fps (640x360@15fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsLtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt up, expect no change since QP is most limited.
  {
    // Store current sink wants since we expect no change and if there is no
    // change then last_wants() is not updated.
    auto previous_sink_wants = source.sink_wants();
    video_stream_encoder_->TriggerCpuUnderuse();
    timestamp_ms += kFrameInterval150Ms;
    source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
    WaitForEncodedFrame(timestamp_ms);
    EXPECT_THAT(source.sink_wants(), FpsEqResolutionEqTo(previous_sink_wants));
    EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
    EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  }

  // Trigger quality adapt up, expect increased fps (640x360@30fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsGtResolutionEq(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt up and Cpu adapt up since both are most limited,
  // expect increased resolution (960x540@30fps).
  video_stream_encoder_->TriggerQualityHigh();
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionGt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt up and Cpu adapt up since both are most limited,
  // expect no restriction (1280x720fps@30fps).
  video_stream_encoder_->TriggerQualityHigh();
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionGt(source.last_wants()));
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no change.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(4, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AdaptWithTwoReasonsAndDifferentOrder_Resolution) {
  const int kWidth = 640;
  const int kHeight = 360;
  const int kFpsLimit = 15;
  const int64_t kFrameInterval150Ms = 150;
  ASSERT_EQ(video_encoder_config_.simulcast_layers.size(), 1u);
  video_encoder_config_.simulcast_layers[0].width = kWidth;
  video_encoder_config_.simulcast_layers[0].height = kHeight;
  video_encoder_config_.simulcast_layers[0].max_framerate = kDefaultFramerate;
  ConfigureEncoder(video_encoder_config_.Copy());
  int64_t timestamp_ms = kFrameInterval150Ms;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Enable BALANCED preference, no initial limitation.
  AdaptingFrameForwarder source(&time_controller_);
  source.set_adaptation_enabled(true);
  video_stream_encoder_->SetSource(&source, DegradationPreference::BALANCED);
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(kWidth, kHeight);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt down, expect scaled down framerate (640x360@15fps).
  video_stream_encoder_->TriggerCpuOveruse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsMatchesResolutionMax(Eq(kFpsLimit)));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(0, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality adapt down, expect scaled down resolution (480x270@15fps).
  video_stream_encoder_->TriggerQualityLow();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionLt(source.last_wants()));
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger cpu adapt up, expect no change because quality is most limited.
  {
    auto previous_sink_wants = source.sink_wants();
    // Store current sink wants since we expect no change ind if there is no
    // change then last__wants() is not updated.
    video_stream_encoder_->TriggerCpuUnderuse();
    timestamp_ms += kFrameInterval150Ms;
    source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
    WaitForEncodedFrame(timestamp_ms);
    EXPECT_THAT(source.sink_wants(), FpsEqResolutionEqTo(previous_sink_wants));
    EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
    EXPECT_EQ(1, stats_proxy_->GetStats().number_of_quality_adapt_changes);
  }

  // Trigger quality adapt up, expect upscaled resolution (640x360@15fps).
  video_stream_encoder_->TriggerQualityHigh();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsEqResolutionGt(source.last_wants()));
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_TRUE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(1, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger quality and cpu adapt up, expect increased fps (640x360@30fps).
  video_stream_encoder_->TriggerQualityHigh();
  video_stream_encoder_->TriggerCpuUnderuse();
  timestamp_ms += kFrameInterval150Ms;
  source.IncomingCapturedFrame(CreateFrame(timestamp_ms, kWidth, kHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_framerate);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_resolution);
  EXPECT_FALSE(stats_proxy_->GetStats().cpu_limited_framerate);
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  // Trigger adapt up, expect no change.
  video_stream_encoder_->TriggerQualityHigh();
  EXPECT_THAT(source.sink_wants(), FpsMaxResolutionMax());
  EXPECT_EQ(2, stats_proxy_->GetStats().number_of_cpu_adapt_changes);
  EXPECT_EQ(3, stats_proxy_->GetStats().number_of_quality_adapt_changes);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, AcceptsFullHdAdaptedDownSimulcastFrames) {
  const int kFrameWidth = 1920;
  const int kFrameHeight = 1080;
  // 2/3 of 1920.
  const int kAdaptedFrameWidth = 1280;
  // 2/3 of 1080.
  const int kAdaptedFrameHeight = 720;
  const int kFramerate = 24;
  uint64_t ntp_time_ms = 123;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  // Trigger reconfigure encoder (without resetting the entire instance).
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
  video_encoder_config.simulcast_layers[0].max_framerate = kFramerate;
  video_encoder_config.max_bitrate_bps = kTargetBitrate.bps();
  video_encoder_config.video_stream_factory =
      make_ref_counted<CroppingVideoStreamFactory>();
  video_stream_encoder_->ConfigureEncoder(std::move(video_encoder_config),
                                          kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_source_.set_adaptation_enabled(true);

  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(kFrameWidth, kFrameHeight);

  // Trigger CPU overuse, downscale by 3/4.
  video_stream_encoder_->TriggerCpuOveruse();
  ntp_time_ms += 1000 / kFramerate;
  video_source_.IncomingCapturedFrame(
      CreateFrame(ntp_time_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(kAdaptedFrameWidth, kAdaptedFrameHeight);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, PeriodicallyUpdatesChannelParameters) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  const int kLowFps = 2;
  const int kHighFps = 30;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  int64_t timestamp_ms = CurrentTimeMs();
  max_framerate_ = kLowFps;

  // Insert 2 seconds of 2fps video.
  for (int i = 0; i < kLowFps * 2; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    WaitForEncodedFrame(timestamp_ms);
    timestamp_ms += 1000 / kLowFps;
  }

  // Make sure encoder is updated with new target.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(timestamp_ms);
  timestamp_ms += 1000 / kLowFps;

  EXPECT_EQ(kLowFps, fake_encoder_.GetConfiguredInputFramerate());

  // Insert 30fps frames for just a little more than the forced update period.
  const int kVcmTimerIntervalFrames = (kProcessIntervalMs * kHighFps) / 1000;
  constexpr TimeDelta kFrameInterval = TimeDelta::Seconds(1) / kHighFps;
  max_framerate_ = kHighFps;
  for (int i = 0; i < kVcmTimerIntervalFrames + 2; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    // Wait for encoded frame, but skip ahead if it doesn't arrive as it might
    // be dropped if the encoder hans't been updated with the new higher target
    // framerate yet, causing it to overshoot the target bitrate and then
    // suffering the wrath of the media optimizer.
    TimedWaitForEncodedFrame(timestamp_ms, 2 * kFrameInterval);
    timestamp_ms += kFrameInterval.ms();
  }

  // Don expect correct measurement just yet, but it should be higher than
  // before.
  EXPECT_GT(fake_encoder_.GetConfiguredInputFramerate(), kLowFps);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DoesNotUpdateBitrateAllocationWhenSuspended) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  ResetEncoder("FAKE", 1, 1, 1, false, kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::
                   kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Insert a first video frame, causes another bitrate update.
  int64_t timestamp_ms = CurrentTimeMs();
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);

  // Next, simulate video suspension due to pacer queue overrun.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::Zero(), DataRate::Zero(), 0, 1, 0);

  // Skip ahead until a new periodic parameter update should have occured.
  timestamp_ms += kProcessIntervalMs;
  AdvanceTime(TimeDelta::Millis(kProcessIntervalMs));

  // No more allocations has been made.
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  ExpectDroppedFrame();
  EXPECT_EQ(sink_.number_of_layers_allocations(), 1);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       DefaultCpuAdaptationThresholdsForSoftwareEncoder) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  const CpuOveruseOptions default_options;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(1);
  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .low_encode_usage_threshold_percent,
            default_options.low_encode_usage_threshold_percent);
  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .high_encode_usage_threshold_percent,
            default_options.high_encode_usage_threshold_percent);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       HigherCpuAdaptationThresholdsForHardwareEncoder) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  CpuOveruseOptions hardware_options;
  hardware_options.low_encode_usage_threshold_percent = 150;
  hardware_options.high_encode_usage_threshold_percent = 200;
  fake_encoder_.SetIsHardwareAccelerated(true);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(1);
  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .low_encode_usage_threshold_percent,
            hardware_options.low_encode_usage_threshold_percent);
  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .high_encode_usage_threshold_percent,
            hardware_options.high_encode_usage_threshold_percent);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       CpuAdaptationThresholdsUpdatesWhenHardwareAccelerationChange) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  const CpuOveruseOptions default_options;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(1);
  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .low_encode_usage_threshold_percent,
            default_options.low_encode_usage_threshold_percent);
  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .high_encode_usage_threshold_percent,
            default_options.high_encode_usage_threshold_percent);

  CpuOveruseOptions hardware_options;
  hardware_options.low_encode_usage_threshold_percent = 150;
  hardware_options.high_encode_usage_threshold_percent = 200;
  fake_encoder_.SetIsHardwareAccelerated(true);

  video_source_.IncomingCapturedFrame(
      CreateFrame(2, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(2);

  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .low_encode_usage_threshold_percent,
            hardware_options.low_encode_usage_threshold_percent);
  EXPECT_EQ(video_stream_encoder_->overuse_detector_proxy_->GetOptions()
                .high_encode_usage_threshold_percent,
            hardware_options.high_encode_usage_threshold_percent);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DropsFramesWhenEncoderOvershoots) {
  const int kFrameWidth = 320;
  const int kFrameHeight = 240;
  const int kFps = 30;
  const int kNumFramesInRun = kFps * 5;  // Runs of five seconds.

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(120),

      /*link_allocation=*/DataRate::KilobitsPerSec(120),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  int64_t timestamp_ms = CurrentTimeMs();
  max_framerate_ = kFps;

  // Insert 3 seconds of video, verify number of drops with normal bitrate.
  fake_encoder_.SimulateOvershoot(1.0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  int num_dropped = 0;
  for (int i = 0; i < kNumFramesInRun; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    // Wait up to two frame durations for a frame to arrive.
    if (!TimedWaitForEncodedFrame(timestamp_ms,
                                  2 * TimeDelta::Seconds(1) / kFps)) {
      ++num_dropped;
    }
    timestamp_ms += 1000 / kFps;
  }

  // Framerate should be measured to be near the expected target rate.
  EXPECT_NEAR(fake_encoder_.GetLastFramerate(), kFps, 1);

  // Frame drops should be within 5% of expected 0%.
  EXPECT_NEAR(num_dropped, 0, 5 * kNumFramesInRun / 100);

  // Make encoder produce frames at double the expected bitrate during 3 seconds
  // of video, verify number of drops. Rate needs to be slightly changed in
  // order to force the rate to be reconfigured.
  double overshoot_factor = 2.0;
  const RateControlSettings trials(env_.field_trials());
  if (trials.UseEncoderBitrateAdjuster()) {
    // With bitrate adjuster, when need to overshoot even more to trigger
    // frame dropping since the adjuter will try to just lower the target
    // bitrate rather than drop frames. If network headroom can be used, it
    // doesn't push back as hard so we don't need quite as much overshoot.
    // These numbers are unfortunately a bit magical but there's not trivial
    // way to algebraically infer them.
    overshoot_factor = 3.5;
  }
  fake_encoder_.SimulateOvershoot(overshoot_factor);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  num_dropped = 0;
  for (int i = 0; i < kNumFramesInRun; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    // Wait up to two frame durations for a frame to arrive.
    if (!TimedWaitForEncodedFrame(timestamp_ms,
                                  2 * TimeDelta::Seconds(1) / kFps)) {
      ++num_dropped;
    }
    timestamp_ms += 1000 / kFps;
  }

  // Target framerate should be still be near the expected target, despite
  // the frame drops.
  EXPECT_NEAR(fake_encoder_.GetLastFramerate(), kFps, 1);

  // Frame drops should be within 5% of expected 50%.
  EXPECT_NEAR(num_dropped, kNumFramesInRun / 2, 5 * kNumFramesInRun / 100);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, ConfiguresCorrectFrameRate) {
  const int kFrameWidth = 320;
  const int kFrameHeight = 240;
  const int kActualInputFps = 24;

  ASSERT_GT(max_framerate_, kActualInputFps);

  int64_t timestamp_ms = CurrentTimeMs();
  max_framerate_ = kActualInputFps;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(120),

      /*link_allocation=*/DataRate::KilobitsPerSec(120),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  // Insert 3 seconds of video, with an input fps lower than configured max.
  for (int i = 0; i < kActualInputFps * 3; ++i) {
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
    // Wait up to two frame durations for a frame to arrive.
    WaitForEncodedFrame(timestamp_ms);
    timestamp_ms += 1000 / kActualInputFps;
  }

  EXPECT_NEAR(kActualInputFps, fake_encoder_.GetLastFramerate(), 1);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, AccumulatesUpdateRectOnDroppedFrames) {
  VideoFrame::UpdateRect rect;
  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  source.IncomingCapturedFrame(CreateFrameWithUpdatedPixel(1, nullptr, 0));
  WaitForEncodedFrame(1);
  // On the very first frame full update should be forced.
  rect = fake_encoder_.GetLastUpdateRect();
  EXPECT_EQ(rect.offset_x, 0);
  EXPECT_EQ(rect.offset_y, 0);
  EXPECT_EQ(rect.height, codec_height_);
  EXPECT_EQ(rect.width, codec_width_);
  // Frame with NTP timestamp 2 will be dropped due to outstanding frames
  // scheduled for processing during encoder queue processing of frame 2.
  source.IncomingCapturedFrame(CreateFrameWithUpdatedPixel(2, nullptr, 1));
  source.IncomingCapturedFrame(CreateFrameWithUpdatedPixel(3, nullptr, 10));
  WaitForEncodedFrame(3);
  // Updates to pixels 1 and 10 should be accumulated to one 10x1 rect.
  rect = fake_encoder_.GetLastUpdateRect();
  EXPECT_EQ(rect.offset_x, 1);
  EXPECT_EQ(rect.offset_y, 0);
  EXPECT_EQ(rect.width, 10);
  EXPECT_EQ(rect.height, 1);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, ForwardsOnFrameDroppedToSink) {
  NiceMock<MockVideoEncoder> video_encoder;
  test::VideoEncoderProxyFactory encoder_factory(&video_encoder);
  video_send_config_.encoder_settings.encoder_factory = &encoder_factory;

  EncodedImageCallback* callback = nullptr;
  ON_CALL(video_encoder, RegisterEncodeCompleteCallback)
      .WillByDefault([&](EncodedImageCallback* cb) {
        callback = cb;
        return WEBRTC_VIDEO_CODEC_OK;
      });

  ON_CALL(video_encoder, Encode)
      .WillByDefault(
          [&](const VideoFrame&, const std::vector<VideoFrameType>*) {
            EXPECT_TRUE(callback != nullptr);
            if (callback) {
              callback->OnFrameDropped(/*rtp_timestamp=*/0, /*spatial_id=*/0,
                                       /*is_end_of_temporal_unit=*/true);
            }
            return WEBRTC_VIDEO_CODEC_OK;
          });

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Trigger Encode by sending a frame.
  video_source_.IncomingCapturedFrame(CreateFrame(1, 1280, 720));

  // Wait for the encoder queue to process the drop.
  AdvanceTime(TimeDelta::Millis(1));

  EXPECT_EQ(sink_.number_of_dropped_frames(), 1);

  video_stream_encoder_->SetSource(
      nullptr, webrtc::DegradationPreference::MAINTAIN_FRAMERATE);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SetsFrameTypes) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // First frame is always keyframe.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_THAT(
      fake_encoder_.LastFrameTypes(),
      ::testing::ElementsAre(VideoFrameType{VideoFrameType::kVideoFrameKey}));

  // Insert delta frame.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_THAT(
      fake_encoder_.LastFrameTypes(),
      ::testing::ElementsAre(VideoFrameType{VideoFrameType::kVideoFrameDelta}));

  // Request next frame be a key-frame.
  video_stream_encoder_->SendKeyFrame();
  video_source_.IncomingCapturedFrame(CreateFrame(3, nullptr));
  WaitForEncodedFrame(3);
  EXPECT_THAT(
      fake_encoder_.LastFrameTypes(),
      ::testing::ElementsAre(VideoFrameType{VideoFrameType::kVideoFrameKey}));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, SetsFrameTypesSimulcast) {
  // Setup simulcast with three streams.
  ResetEncoder("VP8", 3, 1, 1, false);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);
  // Wait for all three layers before triggering event.
  sink_.SetNumExpectedLayers(3);

  // First frame is always keyframe.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  // Insert delta frame.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta}));

  // Request next frame be a key-frame.
  // Only first stream is configured to produce key-frame.
  video_stream_encoder_->SendKeyFrame();
  video_source_.IncomingCapturedFrame(CreateFrame(3, nullptr));
  WaitForEncodedFrame(3);

  // TODO(webrtc:10615): Map keyframe request to spatial layer. Currently
  // keyframe request on any layer triggers keyframe on all layers.
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, MergesKeyFrameRequestsForMultipleLayers) {
  // Setup simulcast with three streams.
  ResetEncoder("VP8", 3, 1, 1, false);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);
  sink_.SetNumExpectedLayers(3);

  // First frame is always keyframe.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  // Insert delta frame.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta}));

  // Request next frame be a keyframe on first and third layers by calling
  // SendKeyFrame twice.
  video_stream_encoder_->SendKeyFrame({VideoFrameType::kVideoFrameKey,
                                       VideoFrameType::kVideoFrameDelta,
                                       VideoFrameType::kVideoFrameDelta});
  video_stream_encoder_->SendKeyFrame({VideoFrameType::kVideoFrameDelta,
                                       VideoFrameType::kVideoFrameDelta,
                                       VideoFrameType::kVideoFrameKey});

  video_source_.IncomingCapturedFrame(CreateFrame(3, nullptr));
  WaitForEncodedFrame(3);

  // Verify that both requests are merged, producing keyframes on 1st and 3rd
  // streams.
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameKey}));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       KeyFrameRequestOnAnyLayerTriggersKeyFrameWhenSingleStream) {
  // Setup encoder with a single stream (e.g., non-simulcast or SVC).
  ResetEncoder("VP8", 1, 1, 1, false);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  sink_.SetNumExpectedLayers(1);

  // First frame is always keyframe.
  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey}));

  // Insert delta frame.
  video_source_.IncomingCapturedFrame(CreateFrame(2, nullptr));
  WaitForEncodedFrame(2);
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameDelta}));

  // Request keyframe on the second layer of a 3-layer RTP configuration.
  // When only a single stream is encoded, any layer requesting a keyframe
  // must trigger a keyframe on that stream.
  video_stream_encoder_->SendKeyFrame({VideoFrameType::kVideoFrameDelta,
                                       VideoFrameType::kVideoFrameKey,
                                       VideoFrameType::kVideoFrameDelta});

  video_source_.IncomingCapturedFrame(CreateFrame(3, nullptr));
  WaitForEncodedFrame(3);

  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey}));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DoesNotRewriteH264BitstreamWithOptimalSps) {
  // SPS contains VUI with restrictions on the maximum number of reordered
  // pictures, there is no need to rewrite the bitstream to enable faster
  // decoding.
  ResetEncoder("H264", 1, 1, 1, false);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  fake_encoder_.SetEncodedImageData(
      EncodedImageBuffer::Create(kOptimalSps, sizeof(kOptimalSps)));

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);

  EXPECT_THAT(sink_.GetLastEncodedImageData(),
              testing::ElementsAreArray(kOptimalSps));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, RewritesH264BitstreamWithNonOptimalSps) {
  // SPS does not contain VUI, the bitstream is will be rewritten with added
  // VUI with restrictions on the maximum number of reordered pictures to
  // enable faster decoding.
  uint8_t original_sps[] = {0,    0,    0,    1,    H264::NaluType::kSps,
                            0x00, 0x00, 0x03, 0x03, 0xF4,
                            0x05, 0x03, 0xC7, 0xC0};
  ResetEncoder("H264", 1, 1, 1, false);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  fake_encoder_.SetEncodedImageData(
      EncodedImageBuffer::Create(original_sps, sizeof(original_sps)));

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);

  EXPECT_THAT(sink_.GetLastEncodedImageData(),
              testing::ElementsAreArray(kOptimalSps));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, CopiesVideoFrameMetadataAfterDownscale) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(300),

      /*link_allocation=*/DataRate::KilobitsPerSec(300),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Insert a first video frame. It should be dropped because of downscale in
  // resolution.
  int64_t timestamp_ms = CurrentTimeMs();
  VideoFrame frame = CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight);
  frame.set_rotation(kVideoRotation_270);
  video_source_.IncomingCapturedFrame(frame);

  ExpectDroppedFrame();

  // Second frame is downscaled.
  timestamp_ms = CurrentTimeMs();
  frame = CreateFrame(timestamp_ms, kFrameWidth / 2, kFrameHeight / 2);
  frame.set_rotation(kVideoRotation_90);
  video_source_.IncomingCapturedFrame(frame);

  WaitForEncodedFrame(timestamp_ms);
  sink_.CheckLastFrameRotationMatches(kVideoRotation_90);

  // Insert another frame, also downscaled.
  timestamp_ms = CurrentTimeMs();
  frame = CreateFrame(timestamp_ms, kFrameWidth / 2, kFrameHeight / 2);
  frame.set_rotation(kVideoRotation_180);
  video_source_.IncomingCapturedFrame(frame);

  WaitForEncodedFrame(timestamp_ms);
  sink_.CheckLastFrameRotationMatches(kVideoRotation_180);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, BandwidthAllocationLowerBound) {
  const int kFrameWidth = 320;
  const int kFrameHeight = 180;

  // Initial rate.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(300),

      /*link_allocation=*/DataRate::KilobitsPerSec(300),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  // Insert a first video frame so that encoder gets configured.
  int64_t timestamp_ms = CurrentTimeMs();
  VideoFrame frame = CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight);
  frame.set_rotation(kVideoRotation_270);
  video_source_.IncomingCapturedFrame(frame);
  WaitForEncodedFrame(timestamp_ms);

  // Set a target rate below the minimum allowed by the codec settings.
  VideoCodec codec_config = fake_encoder_.config();
  DataRate min_rate = DataRate::KilobitsPerSec(codec_config.minBitrate);
  DataRate target_rate = min_rate - DataRate::KilobitsPerSec(1);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/target_rate,

      /*link_allocation=*/target_rate,
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Target bitrate and bandwidth allocation should both be capped at min_rate.
  auto rate_settings = fake_encoder_.GetAndResetLastRateControlSettings();
  ASSERT_TRUE(rate_settings.has_value());
  DataRate allocation_sum =
      DataRate::BitsPerSec(rate_settings->bitrate.get_sum_bps());
  EXPECT_EQ(min_rate, allocation_sum);
  EXPECT_EQ(rate_settings->bandwidth_allocation, min_rate);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderRatesPropagatedOnReconfigure) {
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  // Capture a frame and wait for it to synchronize with the encoder thread.
  int64_t timestamp_ms = CurrentTimeMs();
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, nullptr));
  WaitForEncodedFrame(1);

  auto prev_rate_settings = fake_encoder_.GetAndResetLastRateControlSettings();
  ASSERT_TRUE(prev_rate_settings.has_value());
  EXPECT_EQ(static_cast<int>(prev_rate_settings->framerate_fps),
            kDefaultFramerate);

  // Send 1s of video to ensure the framerate is stable at kDefaultFramerate.
  for (int i = 0; i < 2 * kDefaultFramerate; i++) {
    timestamp_ms += 1000 / kDefaultFramerate;
    video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, nullptr));
    WaitForEncodedFrame(timestamp_ms);
  }
  EXPECT_EQ(static_cast<int>(fake_encoder_.GetLastFramerate()),
            kDefaultFramerate);
  // Capture larger frame to trigger a reconfigure.
  codec_height_ *= 2;
  codec_width_ *= 2;
  timestamp_ms += 1000 / kDefaultFramerate;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, nullptr));
  WaitForEncodedFrame(timestamp_ms);

  EXPECT_EQ(2, sink_.number_of_reconfigurations());
  auto current_rate_settings =
      fake_encoder_.GetAndResetLastRateControlSettings();
  // Ensure we have actually reconfigured twice
  // The rate settings should have been set again even though
  // they haven't changed.
  ASSERT_TRUE(current_rate_settings.has_value());
  EXPECT_EQ(prev_rate_settings, current_rate_settings);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderSelectorCurrentEncoderIsSignaled) {
  constexpr int kDontCare = 100;
  StrictMock<MockEncoderSelector> encoder_selector;
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &fake_encoder_, &encoder_selector);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());

  EXPECT_CALL(encoder_selector, OnCurrentEncoder);

  video_source_.IncomingCapturedFrame(
      CreateFrame(kDontCare, kDontCare, kDontCare));
  AdvanceTime(TimeDelta::Zero());
  video_stream_encoder_->Stop();

  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest, EncoderSelectorBitrateSwitch) {
  constexpr int kDontCare = 100;

  NiceMock<MockEncoderSelector> encoder_selector;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &fake_encoder_, &encoder_selector);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());

  ON_CALL(encoder_selector, OnAvailableBitrate)
      .WillByDefault(Return(SdpVideoFormat("AV1")));
  EXPECT_CALL(switch_callback,
              Call(Optional(Field(&SdpVideoFormat::name, "AV1")),
                   /*allow_default_fallback=*/false));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(50),

      /*link_allocation=*/DataRate::KilobitsPerSec(kDontCare),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  AdvanceTime(TimeDelta::Zero());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderSelectorResolutionSwitch) {
  NiceMock<MockEncoderSelector> encoder_selector;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &fake_encoder_, &encoder_selector);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());

  EXPECT_CALL(encoder_selector, OnResolutionChange(RenderResolution(640, 480)))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(encoder_selector, OnResolutionChange(RenderResolution(320, 240)))
      .WillOnce(Return(SdpVideoFormat("AV1")));
  EXPECT_CALL(switch_callback,
              Call(Optional(Field(&SdpVideoFormat::name, "AV1")),
                   /*allow_default_fallback=*/false));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(800),

      /*link_allocation=*/DataRate::KilobitsPerSec(1000),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  video_source_.IncomingCapturedFrame(CreateFrame(1, 640, 480));
  video_source_.IncomingCapturedFrame(CreateFrame(2, 640, 480));
  video_source_.IncomingCapturedFrame(CreateFrame(3, 320, 240));

  AdvanceTime(TimeDelta::Zero());

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderSelectorBrokenEncoderSwitch) {
  constexpr int kSufficientBitrateToNotDrop = 1000;
  constexpr int kDontCare = 100;

  NiceMock<MockVideoEncoder> video_encoder;
  NiceMock<MockEncoderSelector> encoder_selector;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &video_encoder, &encoder_selector);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());

  // The VideoStreamEncoder needs some bitrate before it can start encoding,
  // setting some bitrate so that subsequent calls to WaitForEncodedFrame does
  // not fail.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),

      /*link_allocation=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  ON_CALL(video_encoder, Encode)
      .WillByDefault(Return(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE));
  ON_CALL(encoder_selector, OnEncoderBroken)
      .WillByDefault(Return(SdpVideoFormat("AV2")));

  Event encode_attempted;
  EXPECT_CALL(switch_callback,
              Call(Optional(Field(&SdpVideoFormat::name, "AV2")),
                   /*allow_default_fallback=*/true))
      .WillOnce([&encode_attempted]() { encode_attempted.Set(); });

  video_source_.IncomingCapturedFrame(CreateFrame(1, kDontCare, kDontCare));
  encode_attempted.Wait(TimeDelta::Seconds(3));

  AdvanceTime(TimeDelta::Zero());

  video_stream_encoder_->Stop();

  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest, SwitchEncoderOnInitFailureWithEncoderSelector) {
  NiceMock<MockVideoEncoder> video_encoder;
  NiceMock<MockEncoderSelector> encoder_selector;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &video_encoder, &encoder_selector);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  ASSERT_EQ(0, sink_.number_of_reconfigurations());

  ON_CALL(video_encoder, InitEncode(_, _))
      .WillByDefault(Return(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE));
  ON_CALL(encoder_selector, OnEncoderBroken)
      .WillByDefault(Return(SdpVideoFormat("AV2")));

  Event encode_attempted;
  EXPECT_CALL(switch_callback,
              Call(Optional(Field(&SdpVideoFormat::name, "AV2")),
                   /*allow_default_fallback=*/true))
      .WillOnce([&encode_attempted]() { encode_attempted.Set(); });

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  encode_attempted.Wait(TimeDelta::Seconds(3));

  AdvanceTime(TimeDelta::Zero());

  video_stream_encoder_->Stop();

  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest,
       SwitchEncoderOnInitFailureWithoutEncoderSelector) {
  auto field_trials = SetFieldTrial(
      "WebRTC-SwitchEncoderFollowCodecPreferenceOrder", "Disabled");

  NiceMock<MockVideoEncoder> video_encoder;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &video_encoder, /*encoder_selector=*/nullptr);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
                   /* num_cores= */ 1, &field_trials);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  ASSERT_EQ(0, sink_.number_of_reconfigurations());

  ON_CALL(video_encoder, InitEncode(_, _))
      .WillByDefault(Return(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE));

  Event encode_attempted;
  EXPECT_CALL(switch_callback,
              Call(Optional(Field(&SdpVideoFormat::name, "VP8")),
                   /*allow_default_fallback=*/true))
      .WillOnce([&encode_attempted]() { encode_attempted.Set(); });

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  encode_attempted.Wait(TimeDelta::Seconds(3));

  AdvanceTime(TimeDelta::Zero());

  video_stream_encoder_->Stop();

  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest, NullEncoderReturnSwitch) {
  // As a variant of EncoderSelectorBrokenEncoderSwitch, when a null
  // VideoEncoder is passed in encoder_factory, it checks whether
  // Codec Switch occurs without a crash.
  constexpr int kSufficientBitrateToNotDrop = 1000;
  constexpr int kDontCare = 100;

  NiceMock<MockEncoderSelector> encoder_selector;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory =
      std::make_unique<test::VideoEncoderNullableProxyFactory>(
          /*encoder=*/nullptr, &encoder_selector);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());
  // The VideoStreamEncoder needs some bitrate before it can start encoding,
  // setting some bitrate so that subsequent calls to WaitForEncodedFrame does
  // not fail.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),

      /*link_allocation=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  ON_CALL(encoder_selector, OnEncoderBroken)
      .WillByDefault(Return(SdpVideoFormat("AV2")));
  Event encode_attempted;
  EXPECT_CALL(switch_callback,
              Call(Optional(Field(&SdpVideoFormat::name, "AV2")),
                   /*allow_default_fallback=*/_))
      .WillOnce([&encode_attempted]() { encode_attempted.Set(); });

  video_source_.IncomingCapturedFrame(CreateFrame(1, kDontCare, kDontCare));
  encode_attempted.Wait(TimeDelta::Seconds(3));

  AdvanceTime(TimeDelta::Zero());

  video_stream_encoder_->Stop();

  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest, NoPreferenceDefaultFallbackToVP8Disabled) {
  constexpr int kSufficientBitrateToNotDrop = 1000;
  constexpr int kDontCare = 100;
  constexpr int kNumFrames = 8;

  NiceMock<MockVideoEncoder> video_encoder;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &video_encoder, /*encoder_selector=*/nullptr);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy());

  // The VideoStreamEncoder needs some bitrate before it can start encoding,
  // setting some bitrate so that subsequent calls to WaitForEncodedFrame does
  // not fail.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),

      /*link_allocation=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  EXPECT_CALL(video_encoder, Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE));

  EXPECT_CALL(switch_callback, Call(Eq(std::nullopt), _));

  VideoFrame frame = CreateFrame(1, kDontCare, kDontCare);
  for (int i = 0; i < kNumFrames; ++i) {
    int64_t timestamp_ms = CurrentTimeMs();
    frame.set_ntp_time_ms(timestamp_ms);
    frame.set_timestamp_us(timestamp_ms * 1000);
    video_source_.IncomingCapturedFrame(frame);
    time_controller_.AdvanceTime(TimeDelta::Millis(33));
  }

  ASSERT_TRUE(WaitUntil([&] { return fake_encoder_.GetNumEncodes() == 0; }));

  // After requesting fallback failure, the encoder will be released.
  EXPECT_CALL(video_encoder, Release()).Times(1);

  AdvanceTime(TimeDelta::Zero());
  video_stream_encoder_->Stop();
  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest,
       NoPreferenceDefaultFallbackToVP8DisabledReconfigureEncoder) {
  constexpr int kSufficientBitrateToNotDrop = 1000;
  constexpr int kDontCare = 100;
  constexpr int kNumFrames = 8;

  auto field_trials = SetFieldTrial(
      "WebRTC-SwitchEncoderFollowCodecPreferenceOrder", "Enabled");

  NiceMock<MockVideoEncoder> video_encoder;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &video_encoder, /*encoder_selector=*/nullptr);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
                   /* num_cores= */ 1, &field_trials);

  // The VideoStreamEncoder needs some bitrate before it can start encoding,
  // setting some bitrate so that subsequent calls to WaitForEncodedFrame does
  // not fail.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),

      /*link_allocation=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  EXPECT_CALL(video_encoder, Encode)
      .Times(kNumFrames + 1)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE))
      .WillRepeatedly(Return(WEBRTC_VIDEO_CODEC_OK));

  EXPECT_CALL(switch_callback, Call(Eq(std::nullopt), _));

  // Encode() will be called once and will return a failure code. All subsequent
  // frames will be dropped.
  VideoFrame frame = CreateFrame(1, kDontCare, kDontCare);
  for (int i = 0; i < kNumFrames; ++i) {
    int64_t timestamp_ms = CurrentTimeMs();
    frame.set_ntp_time_ms(timestamp_ms);
    frame.set_timestamp_us(timestamp_ms * 1000);
    video_source_.IncomingCapturedFrame(frame);
    time_controller_.AdvanceTime(TimeDelta::Millis(33));
  }

  // Encoder reconfiguration is triggered by frame size change. After the
  // encoder is reconfigured, frame encoding will resume.
  frame = CreateFrame(1, 2 * kDontCare, 2 * kDontCare);
  for (int i = 0; i < kNumFrames; ++i) {
    int64_t timestamp_ms = CurrentTimeMs();
    frame.set_ntp_time_ms(timestamp_ms);
    frame.set_timestamp_us(timestamp_ms * 1000);
    video_source_.IncomingCapturedFrame(frame);
    time_controller_.AdvanceTime(TimeDelta::Millis(33));
  }

  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // After requesting fallback failure, the encoder will be released.
  EXPECT_CALL(video_encoder, Release()).Times(1);

  AdvanceTime(TimeDelta::Zero());
  video_stream_encoder_->Stop();
  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest, NoPreferenceDefaultFallbackToVP8Enabled) {
  constexpr int kSufficientBitrateToNotDrop = 1000;
  constexpr int kDontCare = 100;

  auto field_trials = SetFieldTrial(
      "WebRTC-SwitchEncoderFollowCodecPreferenceOrder", "Disabled");

  NiceMock<MockVideoEncoder> video_encoder;
  StrictMock<MockFunction<void(std::optional<SdpVideoFormat>, bool)>>
      switch_callback;
  encoder_switch_request_callback_ = switch_callback.AsStdFunction();
  auto encoder_factory = std::make_unique<test::VideoEncoderProxyFactory>(
      &video_encoder, /*encoder_selector=*/nullptr);
  video_send_config_.encoder_settings.encoder_factory = encoder_factory.get();
  video_encoder_config_.codec_type = kVideoCodecVP9;

  // Reset encoder for new configuration to take effect.
  ConfigureEncoder(video_encoder_config_.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
                   /* num_cores= */ 1, &field_trials);

  // The VideoStreamEncoder needs some bitrate before it can start encoding,
  // setting some bitrate so that subsequent calls to WaitForEncodedFrame does
  // not fail.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),

      /*link_allocation=*/DataRate::KilobitsPerSec(kSufficientBitrateToNotDrop),
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  EXPECT_CALL(video_encoder, Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE));

  // Fallback request will be asking for switching to VP8.
  EXPECT_CALL(switch_callback,
              Call(Optional(Field(&SdpVideoFormat::name, "VP8")),
                   /*allow_default_fallback=*/true));

  VideoFrame frame = CreateFrame(1, kDontCare, kDontCare);
  video_source_.IncomingCapturedFrame(frame);

  AdvanceTime(TimeDelta::Zero());

  video_stream_encoder_->Stop();
  // The encoders produced by the VideoEncoderProxyFactory have a pointer back
  // to it's factory, so in order for the encoder instance in the
  // `video_stream_encoder_` to be destroyed before the `encoder_factory` we
  // reset the `video_stream_encoder_` here.
  video_stream_encoder_.reset();
}

TEST_F(VideoStreamEncoderTest,
       AllocationPropagatedToEncoderWhenTargetRateChanged) {
  const int kFrameWidth = 320;
  const int kFrameHeight = 180;

  // Set initial rate.
  auto rate = DataRate::KilobitsPerSec(100);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/rate,

      /*link_allocation=*/rate,
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  // Insert a first video frame so that encoder gets configured.
  int64_t timestamp_ms = CurrentTimeMs();
  VideoFrame frame = CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight);
  frame.set_rotation(kVideoRotation_270);
  video_source_.IncomingCapturedFrame(frame);
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(1, fake_encoder_.GetNumSetRates());

  // Change of target bitrate propagates to the encoder.
  auto new_stable_rate = rate - DataRate::KilobitsPerSec(5);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/new_stable_rate,

      /*link_allocation=*/rate,
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(2, fake_encoder_.GetNumSetRates());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       AllocationNotPropagatedToEncoderWhenTargetRateUnchanged) {
  const int kFrameWidth = 320;
  const int kFrameHeight = 180;

  // Set initial rate.
  auto rate = DataRate::KilobitsPerSec(100);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/rate,

      /*link_allocation=*/rate,
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);

  // Insert a first video frame so that encoder gets configured.
  int64_t timestamp_ms = CurrentTimeMs();
  VideoFrame frame = CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight);
  frame.set_rotation(kVideoRotation_270);
  video_source_.IncomingCapturedFrame(frame);
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(1, fake_encoder_.GetNumSetRates());

  // Set a higher target rate without changing the link_allocation. Should not
  // reset encoder's rate.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      /*target_bitrate=*/rate,
      /*link_allocation=*/rate,
      /*fraction_lost=*/0,
      /*round_trip_time_ms=*/0,
      /*cwnd_reduce_ratio=*/0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(1, fake_encoder_.GetNumSetRates());
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, ConfiguresVp9SvcAtOddResolutions) {
  const int kWidth = 720;  // 540p adapted down.
  const int kHeight = 405;
  const int kNumFrames = 3;
  // Works on screenshare mode.
  ResetEncoder("VP9", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/2, /*screenshare=*/true);

  video_source_.set_adaptation_enabled(true);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  VideoFrame frame = CreateFrame(1, kWidth, kHeight);

  // Pass enough frames with the full update to trigger animation detection.
  for (int i = 0; i < kNumFrames; ++i) {
    int64_t timestamp_ms = CurrentTimeMs();
    frame.set_ntp_time_ms(timestamp_ms);
    frame.set_timestamp_us(timestamp_ms * 1000);
    video_source_.IncomingCapturedFrame(frame);
    WaitForEncodedFrame(timestamp_ms);
  }

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderResetAccordingToParameterChange) {
  const float downscale_factors[] = {4.0, 2.0, 1.0};
  const int number_layers =
      sizeof(downscale_factors) / sizeof(downscale_factors[0]);
  VideoEncoderConfig config;
  test::FillEncoderConfiguration(kVideoCodecVP8, number_layers, &config);
  for (int i = 0; i < number_layers; ++i) {
    config.simulcast_layers[i].scale_resolution_down_by = downscale_factors[i];
    config.simulcast_layers[i].active = true;
  }
  config.video_stream_factory = nullptr;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);

  // First initialization.
  // Encoder should be initialized. Next frame should be key frame.
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  int64_t timestamp_ms = kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(1, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  // Disable top layer.
  // Encoder shouldn't be re-initialized. Next frame should be delta frame.
  config.simulcast_layers[number_layers - 1].active = false;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers - 1);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(1, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta}));

  // Re-enable top layer.
  // Encoder should be re-initialized. Next frame should be key frame.
  config.simulcast_layers[number_layers - 1].active = true;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(2, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  // Top layer max rate change.
  // Encoder shouldn't be re-initialized. Next frame should be delta frame.
  config.simulcast_layers[number_layers - 1].max_bitrate_bps -= 100;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(2, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta}));

  // Top layer resolution change.
  // Encoder should be re-initialized. Next frame should be key frame.
  config.simulcast_layers[number_layers - 1].scale_resolution_down_by += 0.1;
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(3, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderResetAccordingToCodecChange) {
  const float downscale_factors[] = {4.0, 2.0, 1.0};
  const int number_layers =
      sizeof(downscale_factors) / sizeof(downscale_factors[0]);
  VideoEncoderConfig config;
  test::FillEncoderConfiguration(kVideoCodecVP8, number_layers, &config);
  for (int i = 0; i < number_layers; ++i) {
    config.simulcast_layers[i].scale_resolution_down_by = downscale_factors[i];
    config.simulcast_layers[i].active = true;
  }
  config.video_stream_factory = nullptr;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);

  // First initialization.
  // Encoder should be initialized. Next frame should be key frame.
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  int64_t timestamp_ms = kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(1, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  // Set VP8 format
  // Encoder should be re-initialized. Next frame should be key frame.
  config.video_format = SdpVideoFormat::VP8();
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(2, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  // No changes format
  // Encoder shouldn't be re-initialized. Next frame should be delta frame.
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(2, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta,
                                           VideoFrameType::kVideoFrameDelta}));

  // Set VP8 and VP9 format for layers
  // Encoder should be re-initialized. Next frame should be key frame.
  for (int i = 0; i < number_layers - 1; i++) {
    config.simulcast_layers[i].video_format = SdpVideoFormat::VP8();
  }
  config.simulcast_layers[number_layers - 1].video_format =
      SdpVideoFormat::VP9Profile0();
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(3, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  // Set VP9 format for layer 0
  // Encoder should be re-initialized. Next frame should be key frame.
  config.simulcast_layers[0].video_format = SdpVideoFormat::VP9Profile0();
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  sink_.SetNumExpectedLayers(number_layers);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(CreateFrame(timestamp_ms, 1280, 720));
  WaitForEncodedFrame(timestamp_ms);
  EXPECT_EQ(4, fake_encoder_.GetNumInitializations());
  EXPECT_THAT(fake_encoder_.LastFrameTypes(),
              ::testing::ElementsAreArray({VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey,
                                           VideoFrameType::kVideoFrameKey}));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderResolutionsExposedInSinglecast) {
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;

  SetUp();
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  // Capturing a frame should reconfigure the encoder and expose the encoder
  // resolution, which is the same as the input frame.
  int64_t timestamp_ms = kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(timestamp_ms);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_THAT(video_source_.sink_wants().resolutions,
              ::testing::ElementsAreArray(
                  {VideoSinkWants::FrameSize(kFrameWidth, kFrameHeight)}));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderResolutionsExposedInSimulcast) {
  // Pick downscale factors such that we never encode at full resolution - this
  // is an interesting use case. The frame resolution influences the encoder
  // resolutions, but if no layer has `scale_resolution_down_by` == 1 then the
  // encoder should not ask for the frame resolution. This allows video frames
  // to have the appearence of one resolution but optimize its internal buffers
  // for what is actually encoded.
  const size_t kNumSimulcastLayers = 3u;
  const float kDownscaleFactors[] = {8.0, 4.0, 2.0};
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  const VideoSinkWants::FrameSize kLayer0Size(
      kFrameWidth / kDownscaleFactors[0], kFrameHeight / kDownscaleFactors[0]);
  const VideoSinkWants::FrameSize kLayer1Size(
      kFrameWidth / kDownscaleFactors[1], kFrameHeight / kDownscaleFactors[1]);
  const VideoSinkWants::FrameSize kLayer2Size(
      kFrameWidth / kDownscaleFactors[2], kFrameHeight / kDownscaleFactors[2]);

  VideoEncoderConfig config;
  test::FillEncoderConfiguration(kVideoCodecVP8, kNumSimulcastLayers, &config);
  for (size_t i = 0; i < kNumSimulcastLayers; ++i) {
    config.simulcast_layers[i].scale_resolution_down_by = kDownscaleFactors[i];
    config.simulcast_layers[i].active = true;
  }
  config.video_stream_factory = nullptr;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);

  // Capture a frame with all layers active.
  int64_t timestamp_ms = kFrameIntervalMs;
  sink_.SetNumExpectedLayers(kNumSimulcastLayers);
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(timestamp_ms);
  // Expect encoded resolutions to match the expected simulcast layers.
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_THAT(
      video_source_.sink_wants().resolutions,
      ::testing::ElementsAreArray({kLayer0Size, kLayer1Size, kLayer2Size}));

  // Capture a frame with one of the layers inactive.
  timestamp_ms += kFrameIntervalMs;
  config.simulcast_layers[2].active = false;
  sink_.SetNumExpectedLayers(kNumSimulcastLayers - 1);
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(timestamp_ms);

  // Expect encoded resolutions to match the expected simulcast layers.
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_THAT(video_source_.sink_wants().resolutions,
              ::testing::ElementsAreArray({kLayer0Size, kLayer1Size}));

  // Capture a frame with all but one layer turned off.
  timestamp_ms += kFrameIntervalMs;
  config.simulcast_layers[1].active = false;
  sink_.SetNumExpectedLayers(kNumSimulcastLayers - 2);
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(timestamp_ms);

  // Expect encoded resolutions to match the expected simulcast layers.
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_THAT(video_source_.sink_wants().resolutions,
              ::testing::ElementsAreArray({kLayer0Size}));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, QpPresent_QpKept) {
  ResetEncoder("VP8", 1, 1, 1, false);

  // Force encoder reconfig.
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Set QP on encoded frame and pass the frame to encode complete callback.
  // Since QP is present QP parsing won't be triggered and the original value
  // should be kept.
  EncodedImage encoded_image;
  encoded_image.qp_ = 123;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(
      kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;
  fake_encoder_.InjectEncodedImage(encoded_image, &codec_info);
  EXPECT_TRUE(sink_.WaitForFrame(kDefaultTimeout));
  EXPECT_EQ(sink_.GetLastEncodedImage().qp_, 123);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, QpAbsent_QpParsed) {
  ResetEncoder("VP8", 1, 1, 1, false);

  // Force encoder reconfig.
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Pass an encoded frame without QP to encode complete callback. QP should be
  // parsed and set.
  EncodedImage encoded_image;
  encoded_image.qp_ = -1;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(
      kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;
  fake_encoder_.InjectEncodedImage(encoded_image, &codec_info);
  EXPECT_TRUE(sink_.WaitForFrame(kDefaultTimeout));
  EXPECT_EQ(sink_.GetLastEncodedImage().qp_, 25);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, QpAbsentParsingDisabled_QpAbsent) {
  auto field_trials = SetFieldTrial("WebRTC-QpParsingKillSwitch", "Enabled");

  ResetEncoder("VP8", 1, 1, 1, false, kDefaultFramerate,
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /* num_cores= */ 1, &field_trials);

  // Force encoder reconfig.
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, codec_width_, codec_height_));
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  EncodedImage encoded_image;
  encoded_image.qp_ = -1;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(
      kCodedFrameVp8Qp25, sizeof(kCodedFrameVp8Qp25)));
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;
  fake_encoder_.InjectEncodedImage(encoded_image, &codec_info);
  EXPECT_TRUE(sink_.WaitForFrame(kDefaultTimeout));
  EXPECT_EQ(sink_.GetLastEncodedImage().qp_, -1);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       QualityScalingNotAllowed_QualityScalingDisabled) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();
  video_encoder_config.simulcast_layers[0].max_framerate =
      -1;  // No max frame rate.

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Disable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = false;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, QualityScalingNotAllowed_IsQpTrustedSetTrue) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Set QP trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(true);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = false;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       QualityScalingNotAllowedAndQPIsTrusted_BandwidthScalerDisable) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Set QP trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(true);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = false;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       QualityScalingNotAllowedAndQPIsNotTrusted_BandwidthScalerDisable) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Set QP trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(false);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = false;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderProvideLimitsWhenQPIsNotTrusted) {
  // Set QP trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(false);

  const int MinEncBitrateKbps = 30;
  const int MaxEncBitrateKbps = 100;
  const int MinStartBitrateKbp = 50;
  const VideoEncoder::ResolutionBitrateLimits encoder_bitrate_limits(
      /*frame_size_pixels=*/codec_width_ * codec_height_,
      /*min_start_bitrate_bps=*/MinStartBitrateKbp,
      /*min_bitrate_bps=*/MinEncBitrateKbps * 1000,
      /*max_bitrate_bps=*/MaxEncBitrateKbps * 1000);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  fake_encoder_.SetResolutionBitrateLimits({encoder_bitrate_limits});

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecH264, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = MaxEncBitrateKbps * 1000;
  video_encoder_config.simulcast_layers[0].min_bitrate_bps =
      MinEncBitrateKbps * 1000;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_EQ(
      MaxEncBitrateKbps,
      static_cast<int>(bitrate_allocator_factory_.codec_config().maxBitrate));
  EXPECT_EQ(
      MinEncBitrateKbps,
      static_cast<int>(bitrate_allocator_factory_.codec_config().minBitrate));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, EncoderDoesnotProvideLimitsWhenQPIsNotTrusted) {
  // Set QP not trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(false);

  std::optional<VideoEncoder::ResolutionBitrateLimits> suitable_bitrate_limit =
      EncoderInfoSettings::
          GetSinglecastBitrateLimitForResolutionWhenQpIsUntrusted(
              codec_width_ * codec_height_,
              EncoderInfoSettings::
                  GetDefaultSinglecastBitrateLimitsWhenQpIsUntrusted(
                      kVideoCodecH264));
  EXPECT_TRUE(suitable_bitrate_limit.has_value());

  const int max_encoder_bitrate = suitable_bitrate_limit->max_bitrate_bps;
  const int min_encoder_bitrate = suitable_bitrate_limit->min_bitrate_bps;
  const int target_encoder_bitrate = max_encoder_bitrate;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(target_encoder_bitrate),
      DataRate::BitsPerSec(target_encoder_bitrate), 0, 0, 0);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecH264, 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = max_encoder_bitrate;
  video_encoder_config.simulcast_layers[0].min_bitrate_bps =
      min_encoder_bitrate;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_EQ(
      max_encoder_bitrate / 1000,
      static_cast<int>(bitrate_allocator_factory_.codec_config().maxBitrate));
  EXPECT_EQ(
      min_encoder_bitrate / 1000,
      static_cast<int>(bitrate_allocator_factory_.codec_config().minBitrate));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       RespectsLowerThanDefaultMinBitrateWhenQPIsNotTrusted) {
  // Set QP not trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(false);

  std::optional<VideoEncoder::ResolutionBitrateLimits> suitable_bitrate_limit =
      EncoderInfoSettings::
          GetSinglecastBitrateLimitForResolutionWhenQpIsUntrusted(
              codec_width_ * codec_height_,
              EncoderInfoSettings::
                  GetDefaultSinglecastBitrateLimitsWhenQpIsUntrusted(
                      kVideoCodecH264));
  EXPECT_TRUE(suitable_bitrate_limit.has_value());

  const int max_encoder_bitrate = suitable_bitrate_limit->max_bitrate_bps;
  const int min_encoder_bitrate = suitable_bitrate_limit->min_bitrate_bps;
  const int target_encoder_bitrate = max_encoder_bitrate;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::BitsPerSec(target_encoder_bitrate),
      DataRate::BitsPerSec(target_encoder_bitrate), 0, 0, 0);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecH264, 1, &video_encoder_config);
  // Set the max bitrate config to be lower than what the resolution bitrate
  // limits would suggest for the current resolution.
  video_encoder_config.max_bitrate_bps =
      (max_encoder_bitrate + min_encoder_bitrate) / 2;
  video_encoder_config.simulcast_layers[0].min_bitrate_bps =
      min_encoder_bitrate;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength);

  video_source_.IncomingCapturedFrame(CreateFrame(1, nullptr));
  WaitForEncodedFrame(1);
  EXPECT_EQ(
      video_encoder_config.max_bitrate_bps / 1000,
      static_cast<int>(bitrate_allocator_factory_.codec_config().maxBitrate));
  EXPECT_EQ(
      min_encoder_bitrate / 1000,
      static_cast<int>(bitrate_allocator_factory_.codec_config().minBitrate));

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, NormalComplexityVP9WithMoreThanTwoCores) {
  ResetEncoder("VP9", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1,
               /*screenshare=*/false,
               kDefaultFramerate, /*allocation_callback_type=*/
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /*num_cores=*/3);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, /*width=*/320, /*height=*/180));
  WaitForEncodedFrame(1);
  EXPECT_EQ(fake_encoder_.LastEncoderComplexity(),
            VideoCodecComplexity::kComplexityNormal);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       NormalComplexityVP9WhenLowTierOptimizationsAreDisabled) {
  auto field_trials =
      SetFieldTrial("WebRTC-VP9-LowTierOptimizations", "Disabled");

  ResetEncoder("VP9", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1,
               /*screenshare=*/false,
               kDefaultFramerate, /*allocation_callback_type=*/
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /*num_cores=*/2, &field_trials);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, /*width=*/320, /*height=*/180));
  WaitForEncodedFrame(1);
  EXPECT_EQ(fake_encoder_.LastEncoderComplexity(),
            VideoCodecComplexity::kComplexityNormal);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, LowComplexityVP9WithTwoCores) {
  ResetEncoder("VP9", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1,
               /*screenshare=*/false,
               kDefaultFramerate, /*allocation_callback_type=*/
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /*num_cores=*/2);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, /*width=*/320, /*height=*/180));
  WaitForEncodedFrame(1);
  EXPECT_EQ(fake_encoder_.LastEncoderComplexity(),
            VideoCodecComplexity::kComplexityLow);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       LowComplexityVP9WithDynamicSpeedAndLowTierOptimizations) {
  FieldTrials trials(field_trials_);
  trials.Set("WebRTC-VP9-LowTierOptimizations", "Enabled");
  trials.Set("WebRTC-EncoderSpeed", "dynamic_speed:true");

  ResetEncoder("VP9", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1,
               /*screenshare=*/false,
               kDefaultFramerate, /*allocation_callback_type=*/
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /*num_cores=*/2, &trials);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, /*width=*/320, /*height=*/180));
  WaitForEncodedFrame(1);
  EXPECT_EQ(fake_encoder_.LastEncoderComplexity(),
            VideoCodecComplexity::kComplexityLow);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, DynamicSpeedCanOverrideVp9LowComplexity) {
  FieldTrials trials(field_trials_);
  trials.Set("WebRTC-VP9-LowTierOptimizations", "Enabled");
  trials.Set("WebRTC-EncoderSpeed", "dynamic_speed:true,vp9_camera:high");

  ResetEncoder("VP9", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1,
               /*screenshare=*/false,
               kDefaultFramerate, /*allocation_callback_type=*/
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /*num_cores=*/2, &trials);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, /*width=*/320, /*height=*/180));
  WaitForEncodedFrame(1);
  EXPECT_EQ(fake_encoder_.LastEncoderComplexity(),
            VideoCodecComplexity::kComplexityHigh);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, ConfiguresCameraEncoderComplexityViaFieldTrial) {
  auto field_trials = SetFieldTrial("WebRTC-EncoderSpeed",
                                    "av1_camera:high,av1_screenshare:max");

  ResetEncoder("AV1", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1,
               /*screenshare=*/false,
               kDefaultFramerate, /*allocation_callback_type=*/
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /*num_cores=*/2, &field_trials);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, /*width=*/320, /*height=*/180));
  WaitForEncodedFrame(1);
  EXPECT_EQ(fake_encoder_.LastEncoderComplexity(),
            VideoCodecComplexity::kComplexityHigh);
  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       ConfiguresScreenshareEncoderComplexityViaFieldTrial) {
  auto field_trials = SetFieldTrial("WebRTC-EncoderSpeed",
                                    "av1_camera:high,av1_screenshare:max");

  ResetEncoder("AV1", /*num_streams=*/1, /*num_temporal_layers=*/1,
               /*num_spatial_layers=*/1,
               /*screenshare=*/true,
               kDefaultFramerate, /*allocation_callback_type=*/
               VideoStreamEncoder::BitrateAllocationCallbackType::kNone,
               /*num_cores=*/2, &field_trials);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_source_.IncomingCapturedFrame(
      CreateFrame(1, /*width=*/320, /*height=*/180));
  WaitForEncodedFrame(1);
  EXPECT_EQ(fake_encoder_.LastEncoderComplexity(),
            VideoCodecComplexity::kComplexityMax);
  video_stream_encoder_->Stop();
}

#if !defined(WEBRTC_IOS)
// TODO(bugs.webrtc.org/12401): Disabled because WebRTC-Video-QualityScaling is
// disabled by default on iOS.
TEST_F(VideoStreamEncoderTest, QualityScalingAllowed_QualityScalingEnabled) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = true;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, QualityScalingAllowed_IsQpTrustedSetTrue) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Set QP trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(true);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = true;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, QualityScalingAllowed_IsQpTrustedSetFalse) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Set QP not trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(false);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = true;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  // When quality_scaler doesn't work and is_quality_scaling_allowed is
  // true,the bandwidth_quality_scaler_ works,so bw_limited_resolution is true.
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       QualityScalingAllowedAndQPIsTrusted_BandwidthScalerDisable) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Set QP trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(true);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = true;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  // bandwidth_quality_scaler isn't working, but quality_scaler is working.
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       QualityScalingAllowedAndQPIsNotTrusted_BandwidthScalerEnabled) {
  VideoEncoderConfig video_encoder_config = video_encoder_config_.Copy();

  // Disable scaling settings in encoder info.
  fake_encoder_.SetQualityScaling(false);
  // Set QP trusted in encoder info.
  fake_encoder_.SetIsQpTrusted(false);
  // Enable quality scaling in encoder config.
  video_encoder_config.is_quality_scaling_allowed = true;
  ConfigureEncoder(std::move(video_encoder_config));

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  test::FrameForwarder source;
  video_stream_encoder_->SetSource(&source,
                                   DegradationPreference::MAINTAIN_FRAMERATE);
  EXPECT_THAT(source.sink_wants(), UnlimitedSinkWants());
  EXPECT_FALSE(stats_proxy_->GetStats().bw_limited_resolution);

  source.IncomingCapturedFrame(CreateFrame(1, 1280, 720));
  WaitForEncodedFrame(1);
  video_stream_encoder_->TriggerQualityLow();
  EXPECT_TRUE(stats_proxy_->GetStats().bw_limited_resolution);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest,
       RequestsRefreshFrameAfterEarlyDroppedNativeFrame) {
  // Send a native frame before encoder rates have been set. The encoder is
  // seen as paused at this time.
  Event frame_destroyed_event;
  video_source_.IncomingCapturedFrame(CreateFakeNativeFrame(
      /*ntp_time_ms=*/1, &frame_destroyed_event, codec_width_, codec_height_));

  // Frame should be dropped and destroyed.
  ExpectDroppedFrame();
  EXPECT_TRUE(frame_destroyed_event.Wait(kDefaultTimeout));
  EXPECT_EQ(video_source_.refresh_frames_requested_, 0);

  // Set bitrates, unpausing the encoder and triggering a request for a refresh
  // frame.
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();
  EXPECT_EQ(video_source_.refresh_frames_requested_, 1);

  video_stream_encoder_->Stop();
}

TEST_F(VideoStreamEncoderTest, RecreatesEncoderWhenEnableVp9SpatialLayer) {
  // Set up encoder to use VP9 SVC using two spatial layers.
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx=*/0, true);
  fake_encoder_.SetTemporalLayersSupported(/*spatial_idx*/ 1, true);
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(VideoCodecType::kVideoCodecVP9,
                                 /* num_streams*/ 1, &video_encoder_config);
  video_encoder_config.max_bitrate_bps = 2 * kTargetBitrate.bps();
  video_encoder_config.content_type =
      VideoEncoderConfig::ContentType::kRealtimeVideo;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  vp9_settings.numberOfSpatialLayers = 2;
  vp9_settings.numberOfTemporalLayers = 2;
  vp9_settings.interLayerPred = InterLayerPredMode::kOn;
  vp9_settings.automaticResizeOn = false;
  video_encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  video_encoder_config.spatial_layers =
      GetSvcConfig(1280, 720,
                   /*fps=*/30.0,
                   /*first_active_layer=*/0,
                   /*num_spatial_layers=*/2,
                   /*num_temporal_layers=*/3,
                   /*is_screen_sharing=*/false);
  ConfigureEncoder(video_encoder_config.Copy(),
                   VideoStreamEncoder::BitrateAllocationCallbackType::
                       kVideoLayersAllocation);

  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTargetBitrate, kTargetBitrate, 0, 0, 0);

  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(fake_encoder_.GetNumInitializations(), 1);

  // Turn off the top spatial layer. This does not require an encoder reset.
  video_encoder_config.spatial_layers[1].active = false;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength, nullptr);

  time_controller_.AdvanceTime(TimeDelta::Millis(33));
  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(fake_encoder_.GetNumInitializations(), 1);

  // Turn on the top spatial layer again, this does require an encoder reset.
  video_encoder_config.spatial_layers[1].active = true;
  video_stream_encoder_->ConfigureEncoder(video_encoder_config.Copy(),
                                          kMaxPayloadLength, nullptr);

  time_controller_.AdvanceTime(TimeDelta::Millis(33));
  video_source_.IncomingCapturedFrame(CreateFrame(CurrentTimeMs(), 1280, 720));
  WaitForEncodedFrame(CurrentTimeMs());
  EXPECT_EQ(fake_encoder_.GetNumInitializations(), 2);

  video_stream_encoder_->Stop();
}

#endif  // !defined(WEBRTC_IOS)

// Test parameters: (VideoCodecType codec, bool allow_i420_conversion)
class VideoStreamEncoderWithRealEncoderTest
    : public VideoStreamEncoderTest,
      public ::testing::WithParamInterface<std::pair<VideoCodecType, bool>> {
 public:
  VideoStreamEncoderWithRealEncoderTest()
      : VideoStreamEncoderTest(),
        codec_type_(std::get<0>(GetParam())),
        allow_i420_conversion_(std::get<1>(GetParam())) {}

  void SetUp() override {
    VideoStreamEncoderTest::SetUp();
    std::unique_ptr<VideoEncoder> encoder;
    switch (codec_type_) {
      case kVideoCodecVP8:
        encoder = CreateVp8Encoder(env_);
        break;
      case kVideoCodecVP9:
        encoder = CreateVp9Encoder(env_);
        break;
      case kVideoCodecAV1:
        encoder = CreateLibaomAv1Encoder(env_);
        break;
      case kVideoCodecH264:
        encoder = CreateH264Encoder(env_);
        break;
      case kVideoCodecH265:
        // TODO(bugs.webrtc.org/13485): Use a fake encoder
        break;
      default:
        RTC_DCHECK_NOTREACHED();
    }
    ConfigureEncoderAndBitrate(codec_type_, std::move(encoder));
  }

  void TearDown() override {
    video_stream_encoder_->Stop();
    // Ensure `video_stream_encoder_` is destroyed before
    // `encoder_proxy_factory_`.
    video_stream_encoder_.reset();
    VideoStreamEncoderTest::TearDown();
  }

 protected:
  void ConfigureEncoderAndBitrate(VideoCodecType codec_type,
                                  std::unique_ptr<VideoEncoder> encoder) {
    // Configure VSE to use the encoder.
    encoder_ = std::move(encoder);
    encoder_proxy_factory_ = std::make_unique<test::VideoEncoderProxyFactory>(
        encoder_.get(), &encoder_selector_);
    video_send_config_.encoder_settings.encoder_factory =
        encoder_proxy_factory_.get();
    VideoEncoderConfig video_encoder_config;
    test::FillEncoderConfiguration(codec_type, 1, &video_encoder_config);
    video_encoder_config_ = video_encoder_config.Copy();
    ConfigureEncoder(video_encoder_config_.Copy());

    // Set bitrate to ensure frame is not dropped.
    video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
        kTargetBitrate, kTargetBitrate, 0, 0, 0);
  }

  const VideoCodecType codec_type_;
  const bool allow_i420_conversion_;
  NiceMock<MockEncoderSelector> encoder_selector_;
  std::unique_ptr<test::VideoEncoderProxyFactory> encoder_proxy_factory_;
  std::unique_ptr<VideoEncoder> encoder_;
  std::unique_ptr<MockVideoEncoderFactory> mock_encoder_factory_for_multiplex_;
};

TEST_P(VideoStreamEncoderWithRealEncoderTest, EncoderMapsNativeI420) {
  auto native_i420_frame = test::CreateMappableNativeFrame(
      1, VideoFrameBuffer::Type::kI420, codec_width_, codec_height_);
  video_source_.IncomingCapturedFrame(native_i420_frame);
  WaitForEncodedFrame(codec_width_, codec_height_);

  auto mappable_native_buffer =
      test::GetMappableNativeBufferFromVideoFrame(native_i420_frame);
  std::vector<scoped_refptr<VideoFrameBuffer>> mapped_frame_buffers =
      mappable_native_buffer->GetMappedFramedBuffers();
  ASSERT_EQ(mapped_frame_buffers.size(), 1u);
  EXPECT_EQ(mapped_frame_buffers[0]->width(), codec_width_);
  EXPECT_EQ(mapped_frame_buffers[0]->height(), codec_height_);
  EXPECT_EQ(mapped_frame_buffers[0]->type(), VideoFrameBuffer::Type::kI420);
}

TEST_P(VideoStreamEncoderWithRealEncoderTest, EncoderMapsNativeNV12) {
  auto native_nv12_frame = test::CreateMappableNativeFrame(
      1, VideoFrameBuffer::Type::kNV12, codec_width_, codec_height_);
  video_source_.IncomingCapturedFrame(native_nv12_frame);
  WaitForEncodedFrame(codec_width_, codec_height_);

  auto mappable_native_buffer =
      test::GetMappableNativeBufferFromVideoFrame(native_nv12_frame);
  std::vector<scoped_refptr<VideoFrameBuffer>> mapped_frame_buffers =
      mappable_native_buffer->GetMappedFramedBuffers();
  ASSERT_EQ(mapped_frame_buffers.size(), 1u);
  EXPECT_EQ(mapped_frame_buffers[0]->width(), codec_width_);
  EXPECT_EQ(mapped_frame_buffers[0]->height(), codec_height_);
  EXPECT_EQ(mapped_frame_buffers[0]->type(), VideoFrameBuffer::Type::kNV12);

  if (!allow_i420_conversion_) {
    EXPECT_FALSE(mappable_native_buffer->DidConvertToI420());
  }
}

TEST_P(VideoStreamEncoderWithRealEncoderTest, HandlesLayerToggling) {
  const size_t kNumSpatialLayers = 3u;
  const float kDownscaleFactors[] = {4.0, 2.0, 1.0};
  const int kFrameWidth = 1280;
  const int kFrameHeight = 720;
  const VideoSinkWants::FrameSize kLayer0Size(
      kFrameWidth / kDownscaleFactors[0], kFrameHeight / kDownscaleFactors[0]);
  const VideoSinkWants::FrameSize kLayer1Size(
      kFrameWidth / kDownscaleFactors[1], kFrameHeight / kDownscaleFactors[1]);
  const VideoSinkWants::FrameSize kLayer2Size(
      kFrameWidth / kDownscaleFactors[2], kFrameHeight / kDownscaleFactors[2]);

  VideoEncoderConfig config;
  if (codec_type_ == VideoCodecType::kVideoCodecVP9) {
    test::FillEncoderConfiguration(codec_type_, 1, &config);
    config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
    VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
    vp9_settings.numberOfSpatialLayers = kNumSpatialLayers;
    vp9_settings.numberOfTemporalLayers = 3;
    vp9_settings.automaticResizeOn = false;
    config.encoder_specific_settings =
        make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
            vp9_settings);
    config.spatial_layers = GetSvcConfig(kFrameWidth, kFrameHeight,
                                         /*fps=*/30.0,
                                         /*first_active_layer=*/0,
                                         /*num_spatial_layers=*/3,
                                         /*num_temporal_layers=*/3,
                                         /*is_screen_sharing=*/false);
  } else if (codec_type_ == VideoCodecType::kVideoCodecAV1) {
    test::FillEncoderConfiguration(codec_type_, 1, &config);
    config.max_bitrate_bps = kSimulcastTargetBitrate.bps();
    config.spatial_layers = GetSvcConfig(kFrameWidth, kFrameHeight,
                                         /*fps=*/30.0,
                                         /*first_active_layer=*/0,
                                         /*num_spatial_layers=*/3,
                                         /*num_temporal_layers=*/3,
                                         /*is_screen_sharing=*/false);
    config.simulcast_layers[0].scalability_mode = ScalabilityMode::kL3T3_KEY;
  } else {
    // Simulcast for VP8/H264.
    test::FillEncoderConfiguration(codec_type_, kNumSpatialLayers, &config);
    for (size_t i = 0; i < kNumSpatialLayers; ++i) {
      config.simulcast_layers[i].scale_resolution_down_by =
          kDownscaleFactors[i];
      config.simulcast_layers[i].active = true;
    }
    if (codec_type_ == VideoCodecType::kVideoCodecH264) {
      // Turn off frame dropping to prevent flakiness.
      config.frame_drop_enabled = false;
    }
  }

  auto set_layer_active = [&](int layer_idx, bool active) {
    if (codec_type_ == VideoCodecType::kVideoCodecVP9 ||
        codec_type_ == VideoCodecType::kVideoCodecAV1) {
      config.spatial_layers[layer_idx].active = active;
    } else {
      config.simulcast_layers[layer_idx].active = active;
    }
  };

  config.video_stream_factory = nullptr;
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kSimulcastTargetBitrate, kSimulcastTargetBitrate, 0, 0, 0);

  // Capture a frame with all layers active.
  sink_.SetNumExpectedLayers(kNumSpatialLayers);
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  int64_t timestamp_ms = kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));

  WaitForEncodedFrame(kLayer2Size.width, kLayer2Size.height);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Capture a frame with one of the layers inactive.
  set_layer_active(2, false);
  sink_.SetNumExpectedLayers(kNumSpatialLayers - 1);
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(kLayer1Size.width, kLayer1Size.height);

  // New target bitrates signaled based on lower resolution.
  DataRate kTwoLayerBitrate = DataRate::KilobitsPerSec(833);
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      kTwoLayerBitrate, kTwoLayerBitrate, 0, 0, 0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Re-enable the top layer.
  set_layer_active(2, true);
  sink_.SetNumExpectedLayers(kNumSpatialLayers);
  video_stream_encoder_->ConfigureEncoder(config.Copy(), kMaxPayloadLength);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // Bitrate target adjusted back up to enable HD layer...
  video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
      DataRate::KilobitsPerSec(1800), DataRate::KilobitsPerSec(1800), 0, 0, 0);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  // ...then add a new frame.
  timestamp_ms += kFrameIntervalMs;
  video_source_.IncomingCapturedFrame(
      CreateFrame(timestamp_ms, kFrameWidth, kFrameHeight));
  WaitForEncodedFrame(kLayer2Size.width, kLayer2Size.height);
  video_stream_encoder_->WaitUntilTaskQueueIsIdle();

  video_stream_encoder_->Stop();
}

std::string TestParametersVideoCodecAndAllowI420ConversionToString(
    testing::TestParamInfo<std::pair<VideoCodecType, bool>> info) {
  VideoCodecType codec_type = std::get<0>(info.param);
  bool allow_i420_conversion = std::get<1>(info.param);
  return std::string(CodecTypeToPayloadString(codec_type)) +
         (allow_i420_conversion ? "_AllowToI420" : "_DisallowToI420");
}

constexpr std::pair<VideoCodecType, bool> kVP8DisallowConversion =
    std::make_pair(kVideoCodecVP8, /*allow_i420_conversion=*/false);
constexpr std::pair<VideoCodecType, bool> kVP9DisallowConversion =
    std::make_pair(kVideoCodecVP9, /*allow_i420_conversion=*/false);
constexpr std::pair<VideoCodecType, bool> kAV1AllowConversion =
    std::make_pair(kVideoCodecAV1, /*allow_i420_conversion=*/false);
#if defined(WEBRTC_USE_H264)
constexpr std::pair<VideoCodecType, bool> kH264AllowConversion =
    std::make_pair(kVideoCodecH264, /*allow_i420_conversion=*/true);

// The windows compiler does not tolerate #if statements inside the
// INSTANTIATE_TEST_SUITE_P() macro, so we have to have two definitions (with
// and without H264).
INSTANTIATE_TEST_SUITE_P(
    All,
    VideoStreamEncoderWithRealEncoderTest,
    ::testing::Values(kVP8DisallowConversion,
                      kVP9DisallowConversion,
                      kAV1AllowConversion,
                      kH264AllowConversion),
    TestParametersVideoCodecAndAllowI420ConversionToString);
#else
INSTANTIATE_TEST_SUITE_P(
    All,
    VideoStreamEncoderWithRealEncoderTest,
    ::testing::Values(kVP8DisallowConversion,
                      kVP9DisallowConversion,
                      kAV1AllowConversion),
    TestParametersVideoCodecAndAllowI420ConversionToString);
#endif

class ReconfigureSimulcastEncoderTest : public VideoStreamEncoderTest {
 protected:
  void RunSimulcastTest(const std::vector<std::vector<VideoStream>>& configs,
                        const int expected_num_init_encode) {
    ConfigureEncoder(configs[0]);
    OnBitrateUpdated(kTargetBitrate);
    InsertFrameAndWaitForEncoded();
    EXPECT_EQ(1, sink_.number_of_reconfigurations());
    ExpectEqual(bitrate_allocator_factory_.codec_config(), configs[0]);
    EXPECT_EQ(1, fake_encoder_.GetNumInitializations());
    ExpectEqual(fake_encoder_.config(), configs[0]);

    // Reconfigure encoder, the encoder should only be reconfigured if needed.
    ConfigureEncoder(configs[1]);
    InsertFrameAndWaitForEncoded();
    EXPECT_EQ(2, sink_.number_of_reconfigurations());
    ExpectEqual(bitrate_allocator_factory_.codec_config(), configs[1]);
    EXPECT_EQ(expected_num_init_encode, fake_encoder_.GetNumInitializations());
    if (expected_num_init_encode > 1)
      ExpectEqual(fake_encoder_.config(), configs[1]);

    video_stream_encoder_->Stop();
  }

  void ConfigureEncoder(const std::vector<VideoStream>& streams) {
    VideoEncoderConfig config;
    test::FillEncoderConfiguration(kVideoCodecVP8,
                                   /*num_streams=*/streams.size(), &config);
    auto highest_bitrate_stream =
        absl::c_max_element(streams, [](const auto& a, const auto& b) {
          return a.max_bitrate_bps < b.max_bitrate_bps;
        });
    config.max_bitrate_bps = highest_bitrate_stream->max_bitrate_bps;
    config.simulcast_layers = streams;
    config.number_of_streams = streams.size();
    config.video_stream_factory = nullptr;
    video_stream_encoder_->ConfigureEncoder(std::move(config),
                                            kMaxPayloadLength);
  }

  void OnBitrateUpdated(DataRate bitrate) {
    video_stream_encoder_->OnBitrateUpdatedAndWaitForManagedResources(
        bitrate, bitrate, 0, 0, 0);
  }

  void InsertFrameAndWaitForEncoded() {
    timestamp_ms_ += kFrameIntervalMs;
    video_source_.IncomingCapturedFrame(
        CreateFrame(timestamp_ms_, kWidth, kHeight));
    sink_.WaitForEncodedFrame(timestamp_ms_);
  }

  void ExpectEqual(const VideoCodec& actual,
                   const std::vector<VideoStream>& expected) const {
    EXPECT_EQ(actual.numberOfSimulcastStreams, expected.size());
    for (size_t i = 0; i < actual.numberOfSimulcastStreams; ++i) {
      EXPECT_EQ(actual.simulcastStream[i].maxFramerate,
                expected[i].max_framerate);
      EXPECT_EQ(actual.simulcastStream[i].minBitrate * 1000,
                static_cast<unsigned int>(expected[i].min_bitrate_bps));
      EXPECT_EQ(actual.simulcastStream[i].maxBitrate * 1000,
                static_cast<unsigned int>(expected[i].max_bitrate_bps));
      EXPECT_EQ(actual.simulcastStream[i].width,
                kWidth / expected[i].scale_resolution_down_by);
      EXPECT_EQ(actual.simulcastStream[i].height,
                kHeight / expected[i].scale_resolution_down_by);
      EXPECT_EQ(actual.simulcastStream[i].numberOfTemporalLayers,
                expected[i].num_temporal_layers);
      EXPECT_EQ(actual.GetScalabilityMode(), expected[i].scalability_mode);
    }
  }

  VideoStream DefaultConfig() const {
    VideoStream stream;
    stream.max_framerate = 25;
    stream.min_bitrate_bps = 35000;
    stream.max_bitrate_bps = 900000;
    stream.scale_resolution_down_by = 1.0;
    stream.num_temporal_layers = 1;
    stream.bitrate_priority = 1.0;
    stream.scalability_mode = std::nullopt;
    return stream;
  }

  const int kWidth = 640;
  const int kHeight = 360;
  int64_t timestamp_ms_ = 0;
};

TEST_F(ReconfigureSimulcastEncoderTest, ReconfiguredIfMaxFramerateChanges) {
  VideoStream config_before_high = DefaultConfig();
  config_before_high.scale_resolution_down_by = 1;
  config_before_high.max_framerate = 30;
  VideoStream config_before_low = config_before_high;
  config_before_low.scale_resolution_down_by = 2;
  config_before_low.max_framerate = 20;

  // Keep the highest layer's max_framerate so as to not cause a change in
  // VideoCodec::maxFramerate that may influence this test.
  VideoStream config_after_high = config_before_high;
  VideoStream config_after_low = config_before_low;
  config_after_low.max_framerate = 10;

  RunSimulcastTest({{config_before_high, config_before_low},
                    {config_after_high, config_after_low}},
                   /*expected_num_init_encode=*/2);
}

class ReconfigureEncoderTest : public ReconfigureSimulcastEncoderTest {
 public:
  void RunTest(const std::vector<VideoStream>& configs,
               const int expected_num_init_encode) {
    RunSimulcastTest({{configs[0]}, {configs[1]}}, expected_num_init_encode);
  }
};

TEST_F(ReconfigureEncoderTest, NotReconfiguredIfMaxFramerateChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.max_framerate++;

  RunTest({config1, config2}, /*expected_num_init_encode=*/1);
}

TEST_F(ReconfigureEncoderTest, NotReconfiguredIfMinBitrateChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.min_bitrate_bps += 10000;

  RunTest({config1, config2}, /*expected_num_init_encode=*/1);
}

TEST_F(ReconfigureEncoderTest, NotReconfiguredIfMaxBitrateChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.max_bitrate_bps += 100000;

  RunTest({config1, config2}, /*expected_num_init_encode=*/1);
}

TEST_F(ReconfigureEncoderTest, NotReconfiguredIfBitratePriorityChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.bitrate_priority = config1.bitrate_priority.value() * 2.0;

  RunTest({config1, config2}, /*expected_num_init_encode=*/1);
}

TEST_F(ReconfigureEncoderTest, ReconfiguredIfResolutionChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.scale_resolution_down_by *= 2;

  RunTest({config1, config2}, /*expected_num_init_encode=*/2);
}

TEST_F(ReconfigureEncoderTest, ReconfiguredIfNumTemporalLayerChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.num_temporal_layers = config1.num_temporal_layers.value() + 1;

  RunTest({config1, config2}, /*expected_num_init_encode=*/2);
}

TEST_F(ReconfigureEncoderTest, ReconfiguredIfScalabilityModeChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.scalability_mode = ScalabilityMode::kL2T1;

  RunTest({config1, config2}, /*expected_num_init_encode=*/2);
}

TEST_F(ReconfigureEncoderTest,
       UpdatesNumTemporalLayersFromScalabilityModeChanges) {
  VideoStream config1 = DefaultConfig();
  VideoStream config2 = config1;
  config2.scalability_mode = ScalabilityMode::kL1T2;
  config2.num_temporal_layers = 2;

  RunTest({config1, config2}, /*expected_num_init_encode=*/2);
}

// Simple test that just creates and then immediately destroys an encoder.
// The purpose of the test is to make sure that nothing bad happens if the
// initialization step on the encoder queue, doesn't run.
TEST(VideoStreamEncoderSimpleTest, CreateDestroy) {
  class SuperLazyTaskQueue : public TaskQueueBase {
   public:
    SuperLazyTaskQueue() = default;
    ~SuperLazyTaskQueue() override = default;

    absl::string_view queue_name() const override { return "Lazy"; }

   private:
    void Delete() override { delete this; }
    void PostTaskImpl(absl::AnyInvocable<void() &&> task,
                      const PostTaskTraits& traits,
                      const Location& location) override {
      // meh.
    }
    void PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task,
                             TimeDelta delay,
                             const PostDelayedTaskTraits& traits,
                             const Location& location) override {
      ADD_FAILURE();
    }
  };

  // Lots of boiler plate.
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  Environment env = CreateTestEnvironment({.time = &time_controller});
  MockableSendStatisticsProxy stats_proxy(
      env, VideoSendStream::Config(nullptr),
      VideoEncoderConfig::ContentType::kRealtimeVideo);
  SimpleVideoStreamEncoderFactory::MockFakeEncoder mock_fake_encoder(env);
  test::VideoEncoderProxyFactory encoder_factory(&mock_fake_encoder);
  std::unique_ptr<VideoBitrateAllocatorFactory> bitrate_allocator_factory =
      CreateBuiltinVideoBitrateAllocatorFactory();
  VideoStreamEncoderSettings encoder_settings{
      VideoEncoder::Capabilities(false)};
  encoder_settings.encoder_factory = &encoder_factory;
  encoder_settings.bitrate_allocator_factory = bitrate_allocator_factory.get();

  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  EXPECT_CALL((*adapter.get()), Initialize).WillOnce(Return());

  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> encoder_queue(
      new SuperLazyTaskQueue());

  // Construct a VideoStreamEncoder instance and let it go out of scope without
  // doing anything else. This should be fine since the posted init task will
  // simply be deleted.
  VideoStreamEncoder encoder(
      env, 1, &stats_proxy, std::move(encoder_settings),
      std::make_unique<CpuOveruseDetectorProxy>(env, &stats_proxy),
      std::move(adapter), std::move(encoder_queue),
      VideoStreamEncoder::BitrateAllocationCallbackType::
          kVideoLayersAllocation);

  // Stop the encoder explicitly. This additional step tests if we could
  // hang when calling stop and the TQ has been stopped and/or isn't accepting
  // any more tasks.
  encoder.Stop();
}

TEST(VideoStreamEncoderFrameCadenceTest, ActivatesFrameCadenceOnContentType) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  SimpleVideoStreamEncoderFactory factory;
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&video_stream_encoder_callback](
                    FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  auto video_stream_encoder =
      factory.Create(std::move(adapter), &encoder_queue);

  // First a call before we know the frame size and hence cannot compute the
  // number of simulcast layers.
  EXPECT_CALL(*adapter_ptr, SetZeroHertzModeEnabled(Optional(Field(
                                &FrameCadenceAdapterInterface::
                                    ZeroHertzModeParams::num_simulcast_layers,
                                Eq(0u)))));
  VideoEncoderConfig config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &config);
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  video_stream_encoder->ConfigureEncoder(std::move(config), 0);
  factory.DepleteTaskQueues();

  // Then a call as we've computed the number of simulcast layers after a passed
  // frame.
  EXPECT_CALL(*adapter_ptr, SetZeroHertzModeEnabled(Optional(Field(
                                &FrameCadenceAdapterInterface::
                                    ZeroHertzModeParams::num_simulcast_layers,
                                Gt(0u)))));
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/1);
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);

  // Expect a disabled zero-hertz mode after passing realtime video.
  EXPECT_CALL(*adapter_ptr, SetZeroHertzModeEnabled(Eq(std::nullopt)));
  VideoEncoderConfig config2;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &config2);
  config2.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  video_stream_encoder->ConfigureEncoder(std::move(config2), 0);
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/2);
  factory.DepleteTaskQueues();
}

TEST(VideoStreamEncoderFrameCadenceTest,
     AllowZeroHertzVideoEnablesFrameCadence) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  SimpleVideoStreamEncoderFactory factory;
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&](FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  std::unique_ptr<SimpleVideoStreamEncoderFactory::AdaptedVideoStreamEncoder>
      video_stream_encoder = factory.Create(std::move(adapter), &encoder_queue);

  EXPECT_CALL(*adapter_ptr, SetZeroHertzModeEnabled(Optional(Field(
                                &FrameCadenceAdapterInterface::
                                    ZeroHertzModeParams::num_simulcast_layers,
                                Eq(0u)))));
  VideoEncoderConfig config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &config);
  config.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  config.allow_zero_hertz_video = true;
  video_stream_encoder->ConfigureEncoder(std::move(config), 0);
  factory.DepleteTaskQueues();

  EXPECT_CALL(*adapter_ptr, SetZeroHertzModeEnabled(Optional(Field(
                                &FrameCadenceAdapterInterface::
                                    ZeroHertzModeParams::num_simulcast_layers,
                                Gt(0u)))));
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/1);
  factory.DepleteTaskQueues();

  // Expect a disabled zero-hertz mode after passing realtime video without
  // allow_zero_hertz_video.
  EXPECT_CALL(*adapter_ptr, SetZeroHertzModeEnabled(Eq(std::nullopt)));
  VideoEncoderConfig config2;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &config2);
  config2.content_type = VideoEncoderConfig::ContentType::kRealtimeVideo;
  config2.allow_zero_hertz_video = false;
  video_stream_encoder->ConfigureEncoder(std::move(config2), 0);
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/2);
  factory.DepleteTaskQueues();
}

TEST(VideoStreamEncoderFrameCadenceTest,
     ForwardsFramesIntoFrameCadenceAdapter) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  test::FrameForwarder video_source;
  SimpleVideoStreamEncoderFactory factory;
  auto video_stream_encoder = factory.Create(std::move(adapter));
  video_stream_encoder->SetSource(&video_source,
                                  DegradationPreference::MAINTAIN_FRAMERATE);

  EXPECT_CALL(*adapter_ptr, OnFrame);
  auto buffer = make_ref_counted<NV12Buffer>(/*width=*/16, /*height=*/16);
  video_source.IncomingCapturedFrame(
      VideoFrame::Builder().set_video_frame_buffer(std::move(buffer)).build());
}

TEST(VideoStreamEncoderFrameCadenceTest, UsesFrameCadenceAdapterForFrameRate) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  test::FrameForwarder video_source;
  SimpleVideoStreamEncoderFactory factory;
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&video_stream_encoder_callback](
                    FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  auto video_stream_encoder =
      factory.Create(std::move(adapter), &encoder_queue);

  // This is just to make the VSE operational. We'll feed a frame directly by
  // the callback interface.
  video_stream_encoder->SetSource(&video_source,
                                  DegradationPreference::MAINTAIN_FRAMERATE);

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecGeneric, 1, &video_encoder_config);
  video_stream_encoder->ConfigureEncoder(std::move(video_encoder_config),
                                         /*max_data_payload_length=*/1000);

  EXPECT_CALL(*adapter_ptr, GetInputFrameRateFps);
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/1);
  factory.DepleteTaskQueues();
}

TEST(VideoStreamEncoderFrameCadenceTest,
     DeactivatesActivatesLayersOnBitrateChanges) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  SimpleVideoStreamEncoderFactory factory;
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&video_stream_encoder_callback](
                    FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  auto video_stream_encoder =
      factory.Create(std::move(adapter), &encoder_queue);

  // Configure 2 simulcast layers. FillEncoderConfiguration sets min bitrates to
  //  {150000, 450000}.
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 2, &video_encoder_config);
  video_stream_encoder->ConfigureEncoder(video_encoder_config.Copy(),
                                         kMaxPayloadLength);
  // Ensure an encoder is created.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/1);

  // Both layers enabled at 1 MBit/s.
  video_stream_encoder->OnBitrateUpdated(
      DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(1000), 0, 0, 0);
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(0, /*enabled=*/true));
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(1, /*enabled=*/true));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);

  // Layer 1 disabled at 200 KBit/s.
  video_stream_encoder->OnBitrateUpdated(
      DataRate::KilobitsPerSec(200), DataRate::KilobitsPerSec(200), 0, 0, 0);
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(0, /*enabled=*/true));
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(1, /*enabled=*/false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);

  // All layers off at suspended video.
  video_stream_encoder->OnBitrateUpdated(DataRate::Zero(), DataRate::Zero(), 0,
                                         0, 0);
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(0, /*enabled=*/false));
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(1, /*enabled=*/false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);

  // Both layers enabled again back at 1 MBit/s.
  video_stream_encoder->OnBitrateUpdated(
      DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(1000), 0, 0, 0);
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(0, /*enabled=*/true));
  EXPECT_CALL(*adapter_ptr, UpdateLayerStatus(1, /*enabled=*/true));
  factory.DepleteTaskQueues();
}

TEST(VideoStreamEncoderFrameCadenceTest, UpdatesQualityConvergence) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  SimpleVideoStreamEncoderFactory factory;
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&video_stream_encoder_callback](
                    FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  auto video_stream_encoder =
      factory.Create(std::move(adapter), &encoder_queue);

  // Set minimum QP only after InitEncode() has been called, matching real
  // encoders (e.g. LibvpxVp8Encoder, LibvpxVp9Encoder,
  // SimulcastEncoderAdapter).
  constexpr int kEncoderMinQp = kVp8SteadyStateQpThreshold + 5;
  EXPECT_CALL(factory.GetMockFakeEncoder(), GetEncoderInfo).WillRepeatedly([&] {
    VideoEncoder::EncoderInfo info;
    if (factory.GetMockFakeEncoder().GetNumInitializations() > 0) {
      info.min_qp = kEncoderMinQp;
    }
    return info;
  });

  // Configure 2 simulcast layers and setup 1 MBit/s to unpause the encoder.
  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 2, &video_encoder_config);
  video_stream_encoder->ConfigureEncoder(video_encoder_config.Copy(),
                                         kMaxPayloadLength);
  video_stream_encoder->OnBitrateUpdated(
      DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(1000), 0, 0, 0);

  // Pass a frame which has unconverged results.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/1);
  EXPECT_CALL(factory.GetMockFakeEncoder(), EncodeHook)
      .WillRepeatedly([](EncodedImage& encoded_image,
                         scoped_refptr<EncodedImageBuffer> buffer) {
        encoded_image.qp_ = kEncoderMinQp + 1;
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecVP8;
        return codec_specific;
      });
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(0, false));
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(1, false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&factory.GetMockFakeEncoder());

  // Pass a frame which converges in layer 0 and not in layer 1.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/2);
  EXPECT_CALL(factory.GetMockFakeEncoder(), EncodeHook)
      .WillRepeatedly([](EncodedImage& encoded_image,
                         scoped_refptr<EncodedImageBuffer> buffer) {
        // This sets simulcast index 0 content to be at target quality, while
        // index 1 content is not.
        encoded_image.qp_ =
            kEncoderMinQp + (encoded_image.SimulcastIndex() == 0 ? 0 : 1);
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecVP8;
        return codec_specific;
      });
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(0, true));
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(1, false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&factory.GetMockFakeEncoder());
}

TEST(VideoStreamEncoderFrameCadenceTest,
     UpdatesQualityConvergenceOnlyForBaseTemporalLayer) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  SimpleVideoStreamEncoderFactory factory;
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&video_stream_encoder_callback](
                    FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  auto video_stream_encoder =
      factory.Create(std::move(adapter), &encoder_queue);

  EXPECT_CALL(factory.GetMockFakeEncoder(), GetEncoderInfo).WillRepeatedly([&] {
    VideoEncoder::EncoderInfo info =
        factory.GetMockFakeEncoder().FakeEncoder::GetEncoderInfo();
    info.min_qp = kVp8SteadyStateQpThreshold;
    return info;
  });

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 2, &video_encoder_config);
  video_encoder_config.simulcast_layers[0].num_temporal_layers = 2;
  video_encoder_config.simulcast_layers[1].num_temporal_layers = 2;
  video_stream_encoder->ConfigureEncoder(video_encoder_config.Copy(),
                                         kMaxPayloadLength);
  video_stream_encoder->OnBitrateUpdated(
      DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(1000), 0, 0, 0);

  // Frame 1 (keyframe, T0): unconverged QP updates convergence on both layers.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/1);
  EXPECT_CALL(factory.GetMockFakeEncoder(), EncodeHook)
      .WillRepeatedly([](EncodedImage& encoded_image,
                         scoped_refptr<EncodedImageBuffer> buffer) {
        encoded_image.qp_ = kVp8SteadyStateQpThreshold + 1;
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecVP8;
        return codec_specific;
      });
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(0, false));
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(1, false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&factory.GetMockFakeEncoder());

  // Frame 2 (delta frame, T1): higher temporal layer must not update quality
  // convergence even when QP is at target.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/2);
  EXPECT_CALL(factory.GetMockFakeEncoder(), EncodeHook)
      .WillRepeatedly([](EncodedImage& encoded_image,
                         scoped_refptr<EncodedImageBuffer> buffer) {
        encoded_image.qp_ = kVp8SteadyStateQpThreshold;
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecVP8;
        return codec_specific;
      });
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence).Times(0);
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&factory.GetMockFakeEncoder());

  // Frame 3 (delta frame, T0): base temporal layer updates quality convergence.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/3);
  EXPECT_CALL(factory.GetMockFakeEncoder(), EncodeHook)
      .WillRepeatedly([](EncodedImage& encoded_image,
                         scoped_refptr<EncodedImageBuffer> buffer) {
        encoded_image.qp_ = kVp8SteadyStateQpThreshold +
                            (encoded_image.SimulcastIndex() == 0 ? 0 : 1);
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecVP8;
        return codec_specific;
      });
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(0, true));
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(1, false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&factory.GetMockFakeEncoder());
}

TEST(VideoStreamEncoderFrameCadenceTest,
     UpdatesQualityConvergenceForAllTemporalLayersWhenKillSwitchDisabled) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  SimpleVideoStreamEncoderFactory factory(
      "WebRTC-QCM-IgnoreEnhancementTemporalLayers/Disabled/");
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&video_stream_encoder_callback](
                    FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  auto video_stream_encoder =
      factory.Create(std::move(adapter), &encoder_queue);

  EXPECT_CALL(factory.GetMockFakeEncoder(), GetEncoderInfo).WillRepeatedly([&] {
    VideoEncoder::EncoderInfo info =
        factory.GetMockFakeEncoder().FakeEncoder::GetEncoderInfo();
    info.min_qp = kVp8SteadyStateQpThreshold;
    return info;
  });

  VideoEncoderConfig video_encoder_config;
  test::FillEncoderConfiguration(kVideoCodecVP8, 2, &video_encoder_config);
  video_encoder_config.simulcast_layers[0].num_temporal_layers = 2;
  video_encoder_config.simulcast_layers[1].num_temporal_layers = 2;
  video_stream_encoder->ConfigureEncoder(video_encoder_config.Copy(),
                                         kMaxPayloadLength);
  video_stream_encoder->OnBitrateUpdated(
      DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(1000), 0, 0, 0);

  // Frame 1 (keyframe, T0): unconverged QP updates convergence on both layers.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/1);
  EXPECT_CALL(factory.GetMockFakeEncoder(), EncodeHook)
      .WillRepeatedly([](EncodedImage& encoded_image,
                         scoped_refptr<EncodedImageBuffer> buffer) {
        encoded_image.qp_ = kVp8SteadyStateQpThreshold + 1;
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecVP8;
        return codec_specific;
      });
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(0, false));
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(1, false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&factory.GetMockFakeEncoder());

  // Frame 2 (delta frame, T1): with the kill-switch disabled, T1 updates
  // quality convergence.
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/2);
  EXPECT_CALL(factory.GetMockFakeEncoder(), EncodeHook)
      .WillRepeatedly([](EncodedImage& encoded_image,
                         scoped_refptr<EncodedImageBuffer> buffer) {
        encoded_image.qp_ = kVp8SteadyStateQpThreshold +
                            (encoded_image.SimulcastIndex() == 0 ? 0 : 1);
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecVP8;
        return codec_specific;
      });
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(0, true));
  EXPECT_CALL(*adapter_ptr, UpdateLayerQualityConvergence(1, false));
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&factory.GetMockFakeEncoder());
}

TEST(VideoStreamEncoderFrameCadenceTest,
     RequestsRefreshFramesWhenCadenceAdapterInstructs) {
  auto adapter = std::make_unique<MockFrameCadenceAdapter>();
  auto* adapter_ptr = adapter.get();
  MockVideoSourceInterface mock_source;
  SimpleVideoStreamEncoderFactory factory;
  FrameCadenceAdapterInterface::Callback* video_stream_encoder_callback =
      nullptr;
  EXPECT_CALL(*adapter_ptr, Initialize)
      .WillOnce([&video_stream_encoder_callback](
                    FrameCadenceAdapterInterface::Callback* callback) {
        video_stream_encoder_callback = callback;
      });
  TaskQueueBase* encoder_queue = nullptr;
  auto video_stream_encoder =
      factory.Create(std::move(adapter), &encoder_queue);
  video_stream_encoder->SetSource(&mock_source,
                                  DegradationPreference::MAINTAIN_FRAMERATE);
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &config);
  video_stream_encoder->ConfigureEncoder(std::move(config), 0);
  PassAFrame(encoder_queue, video_stream_encoder_callback, /*ntp_time_ms=*/2);
  // Ensure the encoder is set up.
  factory.DepleteTaskQueues();

  EXPECT_CALL(*adapter_ptr, ProcessKeyFrameRequest)
      .WillOnce([video_stream_encoder_callback] {
        video_stream_encoder_callback->RequestRefreshFrame();
      });
  EXPECT_CALL(mock_source, RequestRefreshFrame);
  video_stream_encoder->SendKeyFrame();
  factory.DepleteTaskQueues();
  Mock::VerifyAndClearExpectations(adapter_ptr);
  Mock::VerifyAndClearExpectations(&mock_source);

  EXPECT_CALL(*adapter_ptr, ProcessKeyFrameRequest);
  EXPECT_CALL(mock_source, RequestRefreshFrame).Times(0);
  video_stream_encoder->SendKeyFrame();
  factory.DepleteTaskQueues();
}

TEST(VideoStreamEncoderFrameCadenceTest,
     RequestsRefreshFrameForEarlyZeroHertzKeyFrameRequest) {
  SimpleVideoStreamEncoderFactory factory;
  auto encoder_queue = factory.env().task_queue_factory().CreateTaskQueue(
      "EncoderQueue", TaskQueueFactory::Priority::kNormal);

  auto adapter = FrameCadenceAdapterInterface::Create(
      &factory.env().clock(), encoder_queue.get(),
      /*metronome=*/nullptr, /*worker_queue=*/nullptr,
      factory.env().field_trials());
  FrameCadenceAdapterInterface* adapter_ptr = adapter.get();

  MockVideoSourceInterface mock_source;
  auto video_stream_encoder = factory.CreateWithEncoderQueue(
      std::move(adapter), std::move(encoder_queue));

  video_stream_encoder->SetSource(&mock_source,
                                  DegradationPreference::MAINTAIN_FRAMERATE);
  VideoEncoderConfig config;
  config.content_type = VideoEncoderConfig::ContentType::kScreen;
  test::FillEncoderConfiguration(kVideoCodecVP8, 1, &config);
  video_stream_encoder->ConfigureEncoder(std::move(config), 0);

  // Eventually expect a refresh frame request when requesting a key frame
  // before initializing zero-hertz mode. This can happen in reality because the
  // threads invoking key frame requests and constraints setup aren't
  // synchronized.
  EXPECT_CALL(mock_source, RequestRefreshFrame);
  video_stream_encoder->SendKeyFrame();
  constexpr int kMaxFps = 30;
  adapter_ptr->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFps});
  factory.GetTimeController()->AdvanceTime(
      TimeDelta::Seconds(1) *
      FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod /
      kMaxFps);
}

class VideoStreamEncoderFrameCadenceRestrictionTest : public ::testing::Test {
 public:
  VideoStreamEncoderFrameCadenceRestrictionTest()
      : adapter_ptr_(adapter_.get()),
        fake_resource_(FakeResource::Create("FakeResource")),
        video_stream_encoder_(
            factory_.Create(std::move(adapter_), &encoder_queue_)) {}

  ~VideoStreamEncoderFrameCadenceRestrictionTest() override {
    factory_.DepleteTaskQueues();
  }

  void UpdateVideoSourceRestrictions(VideoSourceRestrictions restrictions) {
    encoder_queue_->PostTask([this, restrictions] {
      RTC_DCHECK_RUN_ON(encoder_queue_);
      video_stream_encoder_->OnVideoSourceRestrictionsUpdated(
          restrictions, VideoAdaptationCounters(), fake_resource_,
          VideoSourceRestrictions());
    });
  }

 protected:
  SimpleVideoStreamEncoderFactory factory_;
  std::unique_ptr<NiceMock<MockFrameCadenceAdapter>> adapter_{
      std::make_unique<NiceMock<MockFrameCadenceAdapter>>()};
  NiceMock<MockFrameCadenceAdapter>* adapter_ptr_;
  TaskQueueBase* encoder_queue_{nullptr};
  scoped_refptr<FakeResource> fake_resource_;
  VideoSourceRestrictions restrictions_;
  std::unique_ptr<SimpleVideoStreamEncoderFactory::AdaptedVideoStreamEncoder>
      video_stream_encoder_;
};

TEST_F(VideoStreamEncoderFrameCadenceRestrictionTest,
       UpdatesVideoSourceRestrictionsUnRestricted) {
  EXPECT_CALL(*adapter_ptr_, UpdateVideoSourceRestrictions(Eq(std::nullopt)));
  UpdateVideoSourceRestrictions(VideoSourceRestrictions());
}

TEST_F(VideoStreamEncoderFrameCadenceRestrictionTest,
       UpdatesVideoSourceRestrictionsWithMaxFrameRateRestriction) {
  restrictions_.set_max_frame_rate(20);
  EXPECT_CALL(*adapter_ptr_, UpdateVideoSourceRestrictions(Optional(20)));
  UpdateVideoSourceRestrictions(restrictions_);
}

TEST_F(VideoStreamEncoderFrameCadenceRestrictionTest,
       UpdatesVideoSourceRestrictionsWithoutMaxFrameRateRestriction) {
  // Restrictions in resolution count as restriction updated, even though the
  // FPS is unlimited.
  restrictions_.set_max_pixels_per_frame(99);
  restrictions_.set_target_pixels_per_frame(101);
  EXPECT_CALL(*adapter_ptr_, UpdateVideoSourceRestrictions(Eq(std::nullopt)));
  UpdateVideoSourceRestrictions(restrictions_);
}

}  // namespace webrtc
