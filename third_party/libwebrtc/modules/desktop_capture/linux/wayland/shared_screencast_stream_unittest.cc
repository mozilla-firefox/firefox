/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/linux/wayland/shared_screencast_stream.h"

#include <libdrm/drm_fourcc.h>
#include <sys/types.h>

#include <cstdint>
#include <memory>

#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "modules/desktop_capture/linux/wayland/test/test_egl_dmabuf.h"
#include "modules/desktop_capture/linux/wayland/test/test_screencast_stream_provider.h"
#include "modules/desktop_capture/rgba_color.h"
#include "modules/desktop_capture/shared_desktop_frame.h"
#include "rtc_base/event.h"
#include "test/gmock.h"
#include "test/gtest.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Ge;

namespace webrtc {

constexpr TimeDelta kShortWait = TimeDelta::Seconds(5);
constexpr TimeDelta kLongWait = TimeDelta::Seconds(15);

constexpr int kBytesPerPixel = 4;
constexpr int32_t kWidth = 800;
constexpr int32_t kHeight = 640;

// crbug.com/webrtc/14568
// The test fails on "MemorySanitizer: use-of-uninitialized-value in
// libpipewire-0.3.so." or on leaks caused by loading PipeWire
// plugins.
#if defined(ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER) || defined(UNDEFINED_SANITIZER)
#define MAYBE_PipeWireStreamTest DISABLED_PipeWireStreamTest
#else
#define MAYBE_PipeWireStreamTest PipeWireStreamTest
#endif

class MAYBE_PipeWireStreamTest : public ::testing::Test,
                                 public TestScreenCastStreamProvider::Observer,
                                 public SharedScreenCastStream::Observer {
 public:
  MAYBE_PipeWireStreamTest() = default;
  ~MAYBE_PipeWireStreamTest() override = default;

  // FakeScreenCastPortal::Observer
  MOCK_METHOD(void, OnBufferAdded, (), (override));
  MOCK_METHOD(void, OnFrameRecorded, (), (override));
  MOCK_METHOD(void, OnStreamReady, (uint32_t stream_node_id), (override));
  MOCK_METHOD(void, OnStartStreaming, (), (override));
  MOCK_METHOD(void, OnStopStreaming, (), (override));

  // SharedScreenCastStream::Observer
  MOCK_METHOD(void, OnCursorPositionChanged, (), (override));
  MOCK_METHOD(void, OnCursorShapeChanged, (), (override));
  MOCK_METHOD(void, OnDesktopFrameChanged, (), (override));
  MOCK_METHOD(void, OnFailedToProcessBuffer, (), (override));
  MOCK_METHOD(void, OnBufferCorruptedMetadata, (), (override));
  MOCK_METHOD(void, OnBufferCorruptedData, (), (override));
  MOCK_METHOD(void, OnEmptyBuffer, (), (override));
  MOCK_METHOD(void, OnStreamConfigured, (), (override));
  MOCK_METHOD(void,
              OnFormatChanged,
              (uint32_t, uint32_t, uint32_t, uint32_t, uint64_t),
              (override));

  void SetUp() override {
    auto shared_screencast_egl_dmabuf = TestEglDmaBuf::CreateDefault();
    shared_screencast_egl_dmabuf_ = shared_screencast_egl_dmabuf.get();
    shared_screencast_stream_ = SharedScreenCastStream::CreateWithEglDmaBuf(
        std::move(shared_screencast_egl_dmabuf));
    shared_screencast_stream_->SetObserver(this);
    test_screencast_stream_provider_ =
        std::make_unique<TestScreenCastStreamProvider>(this, kWidth, kHeight);
  }

  void StartScreenCastStream(uint32_t stream_node_id) {
    shared_screencast_stream_->StartScreenCastStream(stream_node_id);
  }

  void TearDown() override {
    shared_screencast_stream_ = nullptr;
    test_screencast_stream_provider_.reset();
  }

 protected:
  uint recorded_frames_ = 0;
  bool streaming_ = false;
  TestEglDmaBuf* shared_screencast_egl_dmabuf_ = nullptr;
  std::unique_ptr<TestScreenCastStreamProvider>
      test_screencast_stream_provider_;
  scoped_refptr<SharedScreenCastStream> shared_screencast_stream_;
};

TEST_F(MAYBE_PipeWireStreamTest, TestPipeWire) {
  // Set expectations for PipeWire to successfully connect both streams
  Event waitConnectEvent;
  Event waitStartStreamingEvent;
  Event waitStreamParamChangedEvent1;
  Event waitStreamParamChangedEvent2;

  EXPECT_CALL(*this, OnStreamReady(_))
      .WillOnce([this](uint32_t stream_node_id) {
        StartScreenCastStream(stream_node_id);
      });
  EXPECT_CALL(*this, OnStreamConfigured).WillOnce([&waitConnectEvent] {
    waitConnectEvent.Set();
  });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(3));
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreamingEvent] {
    waitStartStreamingEvent.Set();
  });
  // Default format is using BGRA pixel format, 800x600 resolution with 60fps
  // framerate and defaulting to DRM_FORMAT_MOD_LINEAR modifier.
  // This is called twice, because first format changed is not fixated on a
  // particular modifier from the producer side.
  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                                     DRM_FORMAT_MOD_LINEAR))
      .Times(2);  // Default frame rate.

  // Give it some time to connect, the order between these shouldn't matter, but
  // we need to be sure we are connected before we proceed to work with frames.
  waitConnectEvent.Wait(kLongWait);

  // Wait until we start streaming
  waitStartStreamingEvent.Wait(kShortWait);

  Event frameRetrievedEvent;
  EXPECT_CALL(*this, OnFrameRecorded).Times(7);
  EXPECT_CALL(*this, OnDesktopFrameChanged)
      .Times(3)
      .WillRepeatedly([&frameRetrievedEvent] { frameRetrievedEvent.Set(); });

  // Record a frame in FakePipeWireStream
  RgbaColor red_color(0, 0, 255);
  test_screencast_stream_provider_->RecordFrame(red_color);

  // Retrieve a frame from SharedScreenCastStream
  frameRetrievedEvent.Wait(kShortWait);
  std::unique_ptr<SharedDesktopFrame> frame =
      shared_screencast_stream_->CaptureFrame();

  // Check frame parameters
  ASSERT_NE(frame, nullptr);
  ASSERT_NE(frame->data(), nullptr);
  EXPECT_EQ(frame->rect().width(), kWidth);
  EXPECT_EQ(frame->rect().height(), kHeight);
  EXPECT_EQ(frame->stride(), frame->rect().width() * kBytesPerPixel);
  EXPECT_EQ(RgbaColor(frame->data()), red_color);

  // Test DesktopFrameQueue
  RgbaColor green_color(0, 255, 0);
  test_screencast_stream_provider_->RecordFrame(green_color);
  frameRetrievedEvent.Wait(kShortWait);
  std::unique_ptr<SharedDesktopFrame> frame2 =
      shared_screencast_stream_->CaptureFrame();
  ASSERT_NE(frame2, nullptr);
  ASSERT_NE(frame2->data(), nullptr);
  EXPECT_EQ(frame2->rect().width(), kWidth);
  EXPECT_EQ(frame2->rect().height(), kHeight);
  EXPECT_EQ(frame2->stride(), frame->rect().width() * kBytesPerPixel);
  EXPECT_EQ(RgbaColor(frame2->data()), green_color);

  // Thanks to DesktopFrameQueue we should be able to have two frames shared
  EXPECT_EQ(frame->IsShared(), true);
  EXPECT_EQ(frame2->IsShared(), true);
  EXPECT_NE(frame->data(), frame2->data());

  // This should result into overwriting a frame in use
  Event frameRecordedEvent;
  RgbaColor blue_color(255, 0, 0);
  EXPECT_CALL(*this, OnFailedToProcessBuffer).WillOnce([&frameRecordedEvent] {
    frameRecordedEvent.Set();
  });

  test_screencast_stream_provider_->RecordFrame(blue_color);
  frameRecordedEvent.Wait(kShortWait);

  // First frame should be now overwritten with blue color
  frameRetrievedEvent.Wait(kShortWait);
  EXPECT_EQ(RgbaColor(frame->data()), blue_color);

  // Check we don't process faulty buffers
  Event corruptedMetadataFrameEvent;
  EXPECT_CALL(*this, OnBufferCorruptedMetadata)
      .WillOnce([&corruptedMetadataFrameEvent] {
        corruptedMetadataFrameEvent.Set();
      });

  test_screencast_stream_provider_->RecordFrame(
      blue_color, TestScreenCastStreamProvider::CorruptedMetadata);
  corruptedMetadataFrameEvent.Wait(kShortWait);

  Event corruptedDataFrameEvent;
  EXPECT_CALL(*this, OnBufferCorruptedData)
      .WillOnce([&corruptedDataFrameEvent] { corruptedDataFrameEvent.Set(); });

  test_screencast_stream_provider_->RecordFrame(
      blue_color, TestScreenCastStreamProvider::CorruptedData);
  corruptedDataFrameEvent.Wait(kShortWait);

  // Update stream parameters.
  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 0,
                                     DRM_FORMAT_MOD_LINEAR))
      .WillOnce([&waitStreamParamChangedEvent1] {
        waitStreamParamChangedEvent1.Set();
      });
  EXPECT_CALL(*this, OnStopStreaming);
  shared_screencast_stream_->UpdateScreenCastStreamFrameRate(0);
  waitStreamParamChangedEvent1.Wait(kShortWait);

  // Record a frame in FakePipeWireStream
  Event waitStartStreamingEvent2;
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreamingEvent2] {
    waitStartStreamingEvent2.Set();
  });
  Event emptyFrameEvent2;
  EXPECT_CALL(*this, OnBufferCorruptedData).WillOnce([&emptyFrameEvent2] {
    emptyFrameEvent2.Set();
  });
  waitStartStreamingEvent2.Wait(kShortWait);
  test_screencast_stream_provider_->RecordFrame(
      red_color, TestScreenCastStreamProvider::CorruptedData);
  emptyFrameEvent2.Wait(kShortWait);

  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 22,
                                     DRM_FORMAT_MOD_LINEAR))
      .WillOnce([&waitStreamParamChangedEvent2] {
        waitStreamParamChangedEvent2.Set();
      });
  EXPECT_CALL(*this, OnStopStreaming);
  shared_screencast_stream_->UpdateScreenCastStreamFrameRate(22);
  waitStreamParamChangedEvent2.Wait(kShortWait);

  // Record a frame in FakePipeWireStream
  Event waitStartStreamingEvent3;
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreamingEvent3] {
    waitStartStreamingEvent3.Set();
  });
  Event emptyFrameEvent3;
  EXPECT_CALL(*this, OnBufferCorruptedMetadata).WillOnce([&emptyFrameEvent3] {
    emptyFrameEvent3.Set();
  });
  waitStartStreamingEvent3.Wait(kShortWait);
  test_screencast_stream_provider_->RecordFrame(
      red_color, TestScreenCastStreamProvider::CorruptedMetadata);
  emptyFrameEvent3.Wait(kShortWait);

  // Test disconnection from stream
  EXPECT_CALL(*this, OnStopStreaming);
  shared_screencast_stream_->StopScreenCastStream();
}

TEST_F(MAYBE_PipeWireStreamTest, TestModifierFallback) {
  // Set expectations for initial connection with DRM_FORMAT_MOD_LINEAR
  Event waitConnectEvent;
  Event waitStartStreamingEvent;

  EXPECT_CALL(*this, OnStreamReady(_))
      .WillOnce([this](uint32_t stream_node_id) {
        StartScreenCastStream(stream_node_id);
      });
  EXPECT_CALL(*this, OnStreamConfigured).WillOnce([&waitConnectEvent] {
    waitConnectEvent.Set();
  });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(1));
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreamingEvent] {
    waitStartStreamingEvent.Set();
  });
  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                                     DRM_FORMAT_MOD_LINEAR))
      .Times(AtLeast(1));

  waitConnectEvent.Wait(kLongWait);
  waitStartStreamingEvent.Wait(kShortWait);

  // Mark DRM_FORMAT_MOD_LINEAR as failed, expect renegotiation to
  // kTestFailingModifier
  Event waitRenegotiation1;
  EXPECT_CALL(*this, OnStopStreaming);
  EXPECT_CALL(*this,
              OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                              static_cast<uint64_t>(kTestFailingModifier)))
      .Times(AtLeast(1))
      .WillRepeatedly([&waitRenegotiation1] { waitRenegotiation1.Set(); });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(1));
  Event waitStartStreaming2;
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreaming2] {
    waitStartStreaming2.Set();
  });

  // Mark modifier as failed in both producer and consumer
  auto render_device = shared_screencast_egl_dmabuf_->GetRenderDevice();
  if (render_device) {
    render_device->MarkModifierFailed(DRM_FORMAT_MOD_LINEAR);
  }
  test_screencast_stream_provider_->MarkModifierFailed(DRM_FORMAT_MOD_LINEAR);
  waitRenegotiation1.Wait(kShortWait);
  waitStartStreaming2.Wait(kShortWait);

  // Try to record frame with kTestFailingModifier - should fail on
  // import
  Event frameFailedEvent;
  EXPECT_CALL(*this, OnFailedToProcessBuffer).WillOnce([&frameFailedEvent] {
    frameFailedEvent.Set();
  });
  EXPECT_CALL(*this, OnFrameRecorded);

  RgbaColor red_color(0, 0, 255);
  test_screencast_stream_provider_->RecordFrame(red_color);
  frameFailedEvent.Wait(kShortWait);

  // Mark kTestFailingModifier as failed, expect renegotiation to
  // kTestSuccessModifier
  Event waitRenegotiation2;
  EXPECT_CALL(*this, OnStopStreaming);
  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                                     kTestSuccessModifier))
      .Times(AtLeast(1))
      .WillRepeatedly([&waitRenegotiation2] { waitRenegotiation2.Set(); });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(1));
  Event waitStartStreaming3;
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreaming3] {
    waitStartStreaming3.Set();
  });

  // Mark modifier as failed in both producer and consumer
  if (render_device) {
    render_device->MarkModifierFailed(kTestFailingModifier);
  }
  test_screencast_stream_provider_->MarkModifierFailed(kTestFailingModifier);
  waitRenegotiation2.Wait(kShortWait);
  waitStartStreaming3.Wait(kShortWait);

  // Record frame with kTestSuccessModifier
  Event frameSuccessEvent;
  EXPECT_CALL(*this, OnFrameRecorded);
  EXPECT_CALL(*this, OnDesktopFrameChanged).WillOnce([&frameSuccessEvent] {
    frameSuccessEvent.Set();
  });

  RgbaColor green_color(0, 255, 0);
  test_screencast_stream_provider_->RecordFrame(green_color);
  frameSuccessEvent.Wait(kShortWait);

  std::unique_ptr<SharedDesktopFrame> frame =
      shared_screencast_stream_->CaptureFrame();
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(RgbaColor(frame->data()), green_color);

  // A DMA-BUF is imported from its plane descriptors, so a zero chunk size
  // doesn't make it empty
  Event zeroSizeFrameEvent;
  EXPECT_CALL(*this, OnFrameRecorded);
  EXPECT_CALL(*this, OnDesktopFrameChanged).WillOnce([&zeroSizeFrameEvent] {
    zeroSizeFrameEvent.Set();
  });

  test_screencast_stream_provider_->RecordFrame(
      green_color, TestScreenCastStreamProvider::EmptyData);
  zeroSizeFrameEvent.Wait(kShortWait);

  frame = shared_screencast_stream_->CaptureFrame();
  ASSERT_NE(frame, nullptr);

  // Mark kTestSuccessModifier as failed, expect fallback to MemFd (no
  // modifier)
  Event waitRenegotiation3;
  EXPECT_CALL(*this, OnStopStreaming);
  // When falling back to MemFd, modifier should be DRM_FORMAT_MOD_INVALID
  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                                     DRM_FORMAT_MOD_INVALID))
      .Times(AtLeast(1))
      .WillOnce([&waitRenegotiation3] { waitRenegotiation3.Set(); });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(1));
  Event waitStartStreaming4;
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreaming4] {
    waitStartStreaming4.Set();
  });

  if (render_device) {
    render_device->MarkModifierFailed(kTestSuccessModifier);
  }
  test_screencast_stream_provider_->MarkModifierFailed(kTestSuccessModifier);
  waitRenegotiation3.Wait(kShortWait);
  waitStartStreaming4.Wait(kShortWait);

  // Record empty frame with MemFd
  Event emptyFrameEvent;
  EXPECT_CALL(*this, OnFrameRecorded);
  EXPECT_CALL(*this, OnEmptyBuffer).WillOnce([&emptyFrameEvent] {
    emptyFrameEvent.Set();
  });

  RgbaColor blue_color(255, 0, 0);
  test_screencast_stream_provider_->RecordFrame(
      blue_color, TestScreenCastStreamProvider::EmptyData);
  emptyFrameEvent.Wait(kShortWait);

  // Check that a MemFd buffer with an invalid stride is rejected
  Event invalidStrideEvent;
  EXPECT_CALL(*this, OnFrameRecorded);
  EXPECT_CALL(*this, OnFailedToProcessBuffer).WillOnce([&invalidStrideEvent] {
    invalidStrideEvent.Set();
  });

  test_screencast_stream_provider_->RecordFrame(
      blue_color, TestScreenCastStreamProvider::InvalidStride);
  invalidStrideEvent.Wait(kShortWait);

  // Test disconnection from stream
  EXPECT_CALL(*this, OnStopStreaming);
  shared_screencast_stream_->StopScreenCastStream();
}

// Verifies that when the renegotiate event is unavailable (e.g. eventfd
// exhaustion or initialization failure), ProcessDMABuffer gracefully fails
// buffer import without crashing.
TEST_F(MAYBE_PipeWireStreamTest, TestModifierFallbackNullRenegotiateEvent) {
  Event waitConnectEvent;
  Event waitStartStreamingEvent;

  EXPECT_CALL(*this, OnStreamReady(_))
      .WillOnce([this](uint32_t stream_node_id) {
        StartScreenCastStream(stream_node_id);
      });
  EXPECT_CALL(*this, OnStreamConfigured).WillOnce([&waitConnectEvent] {
    waitConnectEvent.Set();
  });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(1));
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreamingEvent] {
    waitStartStreamingEvent.Set();
  });
  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                                     DRM_FORMAT_MOD_LINEAR))
      .Times(AtLeast(1));

  waitConnectEvent.Wait(kLongWait);
  waitStartStreamingEvent.Wait(kShortWait);

  // Clear renegotiate event source to simulate allocation failure / eventfd
  // exhaustion.
  shared_screencast_stream_->ClearRenegotiateEventForTest();

  // Mark DRM_FORMAT_MOD_LINEAR as failed, expect renegotiation to
  // kTestFailingModifier
  Event waitRenegotiation1;
  EXPECT_CALL(*this, OnStopStreaming);
  EXPECT_CALL(*this,
              OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                              static_cast<uint64_t>(kTestFailingModifier)))
      .Times(AtLeast(1))
      .WillRepeatedly([&waitRenegotiation1] { waitRenegotiation1.Set(); });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(1));
  Event waitStartStreaming2;
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreaming2] {
    waitStartStreaming2.Set();
  });

  auto render_device = shared_screencast_egl_dmabuf_->GetRenderDevice();
  if (render_device) {
    render_device->MarkModifierFailed(DRM_FORMAT_MOD_LINEAR);
  }
  test_screencast_stream_provider_->MarkModifierFailed(DRM_FORMAT_MOD_LINEAR);
  waitRenegotiation1.Wait(kShortWait);
  waitStartStreaming2.Wait(kShortWait);

  // Try to record frame with kTestFailingModifier - should fail on
  // import and safely notify OnFailedToProcessBuffer without crashing, even
  // when renegotiate event source is null.
  Event frameFailedEvent;
  EXPECT_CALL(*this, OnFailedToProcessBuffer).WillOnce([&frameFailedEvent] {
    frameFailedEvent.Set();
  });
  EXPECT_CALL(*this, OnFrameRecorded);

  RgbaColor red_color(0, 0, 255);
  test_screencast_stream_provider_->RecordFrame(red_color);
  frameFailedEvent.Wait(kShortWait);

  // Test disconnection from stream
  EXPECT_CALL(*this, OnStopStreaming);
  shared_screencast_stream_->StopScreenCastStream();
}

// Verifies that stopping the stream while a buffer is being processed on
// PipeWire's data loop thread waits for the processing to finish, instead of
// tearing down state that the in-flight OnStreamProcess() still uses.
TEST_F(MAYBE_PipeWireStreamTest, TestStopWhileProcessingBuffer) {
  Event waitConnectEvent;
  Event waitStartStreamingEvent;

  EXPECT_CALL(*this, OnStreamReady(_))
      .WillOnce([this](uint32_t stream_node_id) {
        StartScreenCastStream(stream_node_id);
      });
  EXPECT_CALL(*this, OnStreamConfigured).WillOnce([&waitConnectEvent] {
    waitConnectEvent.Set();
  });
  EXPECT_CALL(*this, OnBufferAdded).Times(AtLeast(1));
  EXPECT_CALL(*this, OnStartStreaming).WillOnce([&waitStartStreamingEvent] {
    waitStartStreamingEvent.Set();
  });
  EXPECT_CALL(*this, OnFormatChanged(SPA_VIDEO_FORMAT_BGRA, 800, 640, 60,
                                     DRM_FORMAT_MOD_LINEAR))
      .Times(AtLeast(1));

  waitConnectEvent.Wait(kLongWait);
  waitStartStreamingEvent.Wait(kShortWait);

  // Block inside OnStreamProcess() on the PipeWire data loop thread, so that
  // StopScreenCastStream() runs while the buffer is still being processed.
  Event processingBufferEvent;
  EXPECT_CALL(*this, OnFrameRecorded);
  EXPECT_CALL(*this, OnBufferCorruptedData).WillOnce([&processingBufferEvent] {
    processingBufferEvent.Set();
    Event().Wait(TimeDelta::Millis(500));
  });

  RgbaColor red_color(0, 0, 255);
  test_screencast_stream_provider_->RecordFrame(
      red_color, TestScreenCastStreamProvider::CorruptedData);
  ASSERT_TRUE(processingBufferEvent.Wait(kShortWait));

  EXPECT_CALL(*this, OnStopStreaming);
  shared_screencast_stream_->StopScreenCastStream();
  EXPECT_EQ(shared_screencast_stream_->CaptureFrame(), nullptr);
}

// The ability to create multiple Pipewire capture streams in the same
// process is needed by projects such as Chrome Remote Desktop.
TEST_F(MAYBE_PipeWireStreamTest, TestMultipleInstancesAllowed) {
  // We need to wait for the provider to be ready so we can start the first
  // stream.
  Event waitConnectEvent;
  EXPECT_CALL(*this, OnStreamReady(_))
      .WillOnce([this](uint32_t stream_node_id) {
        StartScreenCastStream(stream_node_id);
      });
  EXPECT_CALL(*this, OnStreamConfigured).WillOnce([&waitConnectEvent] {
    waitConnectEvent.Set();
  });

  // This will trigger OnStreamReady and call StartScreenCastStream on the first
  // stream. We need to wait for it to be configured to ensure it has started.
  waitConnectEvent.Wait(kLongWait);

  // Now create a second stream.
  auto shared_screencast_egl_dmabuf2 = TestEglDmaBuf::CreateDefault();
  auto shared_screencast_stream2 = SharedScreenCastStream::CreateWithEglDmaBuf(
      std::move(shared_screencast_egl_dmabuf2));

  // Try to start the second stream. Multiple instances should be allowed.
  EXPECT_TRUE(shared_screencast_stream2->StartScreenCastStream(2));

  // Clean up.
  shared_screencast_stream2->StopScreenCastStream();
  shared_screencast_stream_->StopScreenCastStream();
}

}  // namespace webrtc
