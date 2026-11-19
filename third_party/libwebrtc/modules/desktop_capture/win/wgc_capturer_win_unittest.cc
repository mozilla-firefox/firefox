/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/win/wgc_capturer_win.h"

#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api/make_ref_counted.h"
#include "modules/desktop_capture/desktop_capture_options.h"
#include "modules/desktop_capture/desktop_capture_types.h"
#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/fallback_desktop_capturer_wrapper.h"
#include "modules/desktop_capture/frame_texture.h"
#include "modules/desktop_capture/full_screen_window_detector.h"
#include "modules/desktop_capture/win/full_screen_win_application_handler.h"
#include "modules/desktop_capture/win/screen_capture_utils.h"
#include "modules/desktop_capture/win/test_support/test_window.h"
#include "modules/desktop_capture/win/wgc_capture_session.h"
#include "modules/desktop_capture/win/window_capture_utils.h"
#include "rtc_base/logging.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/win/scoped_com_initializer.h"
#include "rtc_base/win/windows_version.h"
#include "system_wrappers/include/metrics.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

constexpr char kWindowThreadName[] = "wgc_capturer_test_window_thread";
constexpr WCHAR kWindowTitle[] = L"WGC Capturer Test Window";

constexpr char kCapturerImplHistogram[] =
    "WebRTC.DesktopCapture.Win.DesktopCapturerImpl";

constexpr char kCapturerResultHistogram[] =
    "WebRTC.DesktopCapture.Win.WgcCapturerResult";
constexpr int kSuccess = 0;
constexpr int kSessionStartFailure = 4;

constexpr char kCaptureSessionResultHistogram[] =
    "WebRTC.DesktopCapture.Win.WgcCaptureSessionStartResult";
constexpr int kSourceClosed = 1;

constexpr char kCaptureTimeHistogram[] =
    "WebRTC.DesktopCapture.Win.WgcCapturerFrameTime";

constexpr char kFullScreenDetectorResult[] =
    "WebRTC.Screenshare.FullScreenDetectorResult";
constexpr int detector_result_success = 1;
constexpr int detector_result_failure_same_title_windows = 2;
constexpr int detector_result_failure_slide_show_not_chosen = 3;

constexpr char kCaptureFullscreenDetectorHistogram[] =
    "WebRTC.Screenshare.DesktopCapturerFullscreenDetector";

constexpr char kWgcWindowFallbackSelectSourceHistogram[] =
    "WebRTC.DesktopCapture.Win.WgcWindowFallbackSelectSource";
using SelectSourceResult = FallbackDesktopCapturerWrapper::SelectSourceResult;

int NumSelectSourceEvents(SelectSourceResult result) {
  return metrics::NumEvents(kWgcWindowFallbackSelectSourceHistogram,
                            static_cast<int>(result));
}

// The capturer keeps `kNumBuffers` in its frame pool, so we need to request
// that many frames to clear those out. The next frame will have the new size
// (if the size has changed) so we will resize the frame pool at this point.
// Then, we need to clear any frames that may have delivered to the frame pool
// before the resize. Finally, the next frame will be guaranteed to be the new
// size.
constexpr int kNumCapturesToFlushBuffers =
    WgcCaptureSession::kNumBuffers * 2 + 1;

constexpr int kSmallWindowWidth = 200;
constexpr int kSmallWindowHeight = 100;
constexpr int kMediumWindowWidth = 300;
constexpr int kMediumWindowHeight = 200;
constexpr int kLargeWindowWidth = 400;
constexpr int kLargeWindowHeight = 500;

// The size of the image we capture is slightly smaller than the actual size of
// the window.
constexpr int kWindowWidthSubtrahend = 14;
constexpr int kWindowHeightSubtrahend = 7;

// Custom message constants so we can direct our thread to close windows and
// quit running.
constexpr UINT kDestroyWindow = WM_APP;
constexpr UINT kQuitRunning = WM_APP + 1;

// When testing changes to real windows, sometimes the effects (close or resize)
// don't happen immediately, we want to keep trying until we see the effect but
// only for a reasonable amount of time.
constexpr int kMaxTries = 50;

}  // namespace

class LogMessageMatcher : public LogSink {
 public:
  explicit LogMessageMatcher(const std::string& substring)
      : substring_(substring) {}
  bool matched() const { return matched_; }

 private:
  void OnLogMessage(const LogLineRef& line) override {
    if (line.message().find(substring_) != std::string::npos) {
      matched_ = true;
    }
  }
  std::string substring_;
  bool matched_ = false;
};

class WgcCapturerWinTest : public ::testing::TestWithParam<CaptureType>,
                           public DesktopCapturer::Callback {
 public:
  void SetUp() override {
    com_initializer_ =
        std::make_unique<ScopedCOMInitializer>(ScopedCOMInitializer::kMTA);
    EXPECT_TRUE(com_initializer_->Succeeded());

    if (!IsWgcSupported(GetParam())) {
      RTC_LOG(LS_INFO)
          << "Skipping WgcCapturerWinTests on unsupported platforms.";
      GTEST_SKIP();
    }
  }

  void SetUpForWindowCapture(int window_width = kMediumWindowWidth,
                             int window_height = kMediumWindowHeight) {
    capturer_ = WgcCapturerWin::CreateRawWindowCapturer(
        DesktopCaptureOptions::CreateDefault());
    CreateWindowOnSeparateThread(window_width, window_height);
    StartWindowThreadMessageLoop();
    source_id_ = GetTestWindowIdFromSourceList();
  }

  void SetUpForScreenCapture(const DesktopCaptureOptions& options =
                                 DesktopCaptureOptions::CreateDefault()) {
    capturer_ = WgcCapturerWin::CreateRawScreenCapturer(options);
    source_id_ = GetScreenIdFromSourceList();
  }

  void TearDown() override {
    if (window_open_) {
      CloseTestWindow();
    }
  }

  // The window must live on a separate thread so that we can run a message pump
  // without blocking the test thread. This is necessary if we are interested in
  // having GraphicsCaptureItem events (i.e. the Closed event) fire, and it more
  // closely resembles how capture works in the wild.
  void CreateWindowOnSeparateThread(int window_width, int window_height) {
    window_thread_ = Thread::Create();
    window_thread_->SetName(kWindowThreadName, nullptr);
    window_thread_->Start();
    SendTask(window_thread_.get(), [this, window_width, window_height]() {
      window_thread_id_ = GetCurrentThreadId();
      window_info_ =
          CreateTestWindow(kWindowTitle, window_height, window_width);
      window_open_ = true;

      while (!IsWindowResponding(window_info_.hwnd)) {
        RTC_LOG(LS_INFO) << "Waiting for test window to become responsive in "
                            "WgcWindowCaptureTest.";
      }

      while (!IsWindowValidAndVisible(window_info_.hwnd)) {
        RTC_LOG(LS_INFO) << "Waiting for test window to be visible in "
                            "WgcWindowCaptureTest.";
      }
    });

    ASSERT_TRUE(window_thread_->RunningForTest());
    ASSERT_FALSE(window_thread_->IsCurrent());
  }

  void StartWindowThreadMessageLoop() {
    window_thread_->PostTask([this]() {
      MSG msg;
      BOOL gm;
      while ((gm = ::GetMessage(&msg, NULL, 0, 0)) != 0 && gm != -1) {
        ::DispatchMessage(&msg);
        if (msg.message == kDestroyWindow) {
          DestroyTestWindow(window_info_);
        }
        if (msg.message == kQuitRunning) {
          PostQuitMessage(0);
        }
      }
    });
  }

  void CloseTestWindow() {
    ::PostThreadMessage(window_thread_id_, kDestroyWindow, 0, 0);
    ::PostThreadMessage(window_thread_id_, kQuitRunning, 0, 0);
    window_thread_->Stop();
    window_open_ = false;
  }

  DesktopCapturer::SourceId GetTestWindowIdFromSourceList() {
    // Frequently, the test window will not show up in GetSourceList because it
    // was created too recently. Since we are confident the window will be found
    // eventually we loop here until we find it.
    intptr_t src_id = 0;
    do {
      DesktopCapturer::SourceList sources;
      EXPECT_TRUE(capturer_->GetSourceList(&sources));
      auto it = std::find_if(
          sources.begin(), sources.end(),
          [&](const DesktopCapturer::Source& src) {
            return src.id == reinterpret_cast<intptr_t>(window_info_.hwnd);
          });

      if (it != sources.end())
        src_id = it->id;
    } while (src_id != reinterpret_cast<intptr_t>(window_info_.hwnd));

    return src_id;
  }

  DesktopCapturer::SourceId GetScreenIdFromSourceList() {
    DesktopCapturer::SourceList sources;
    EXPECT_TRUE(capturer_->GetSourceList(&sources));
    EXPECT_GT(sources.size(), 0ULL);
    return sources[0].id;
  }

  void DoCapture(int num_captures = 1) {
    // Capture the requested number of frames. We expect the first capture to
    // always succeed. If we're asked for multiple frames, we do expect to see a
    // a couple dropped frames due to resizing the window.
    const int max_tries = num_captures == 1 ? 1 : kMaxTries;
    int success_count = 0;
    for (int i = 0; success_count < num_captures && i < max_tries; i++) {
      capturer_->CaptureFrame();
      if (result_ == DesktopCapturer::Result::ERROR_PERMANENT)
        break;
      if (result_ == DesktopCapturer::Result::SUCCESS)
        success_count++;
    }

    total_successful_captures_ += success_count;
    EXPECT_EQ(success_count, num_captures);
    EXPECT_EQ(result_, DesktopCapturer::Result::SUCCESS);
    EXPECT_TRUE(frame_);
    EXPECT_GE(metrics::NumEvents(kCapturerResultHistogram, kSuccess),
              total_successful_captures_);
  }

  void ValidateFrame(int expected_width, int expected_height) {
    EXPECT_EQ(frame_->size().width(), expected_width - kWindowWidthSubtrahend);
    EXPECT_EQ(frame_->size().height(),
              expected_height - kWindowHeightSubtrahend);

    // Verify the buffer contains as much data as it should.
    int data_length = frame_->stride() * frame_->size().height();

    // The first and last pixel should have the same color because they will be
    // from the border of the window.
    // Pixels have 4 bytes of data so the whole pixel needs a uint32_t to fit.
    uint32_t first_pixel = static_cast<uint32_t>(*frame_->data());
    uint32_t last_pixel = static_cast<uint32_t>(
        *(frame_->data() + data_length - DesktopFrame::kBytesPerPixel));
    EXPECT_EQ(first_pixel, last_pixel);

    // Let's also check a pixel from the middle of the content area, which the
    // test window will paint a consistent color for us to verify.
    uint8_t* middle_pixel = frame_->data() + (data_length / 2);

    int sub_pixel_offset = DesktopFrame::kBytesPerPixel / 4;
    EXPECT_EQ(*middle_pixel, kTestWindowBValue);
    middle_pixel += sub_pixel_offset;
    EXPECT_EQ(*middle_pixel, kTestWindowGValue);
    middle_pixel += sub_pixel_offset;
    EXPECT_EQ(*middle_pixel, kTestWindowRValue);
    middle_pixel += sub_pixel_offset;

    // The window is opaque so we expect 0xFF for the Alpha channel.
    EXPECT_EQ(*middle_pixel, 0xFF);
  }

  // DesktopCapturer::Callback interface
  // The capturer synchronously invokes this method before `CaptureFrame()`
  // returns.
  void OnCaptureResult(DesktopCapturer::Result result,
                       std::unique_ptr<DesktopFrame> frame) override {
    result_ = result;
    frame_ = std::move(frame);
  }

 protected:
  std::unique_ptr<ScopedCOMInitializer> com_initializer_;
  DWORD window_thread_id_;
  std::unique_ptr<Thread> window_thread_;
  WindowInfo window_info_;
  intptr_t source_id_;
  bool window_open_ = false;
  DesktopCapturer::Result result_;
  int total_successful_captures_ = 0;
  std::unique_ptr<DesktopFrame> frame_;
  std::unique_ptr<DesktopCapturer> capturer_;
};

TEST_P(WgcCapturerWinTest, SelectValidSource) {
  if (GetParam() == CaptureType::kWindow) {
    SetUpForWindowCapture();
  } else {
    SetUpForScreenCapture();
  }

  EXPECT_TRUE(capturer_->SelectSource(source_id_));
}

TEST_P(WgcCapturerWinTest, SelectInvalidSource) {
  if (GetParam() == CaptureType::kWindow) {
    capturer_ = WgcCapturerWin::CreateRawWindowCapturer(
        DesktopCaptureOptions::CreateDefault());
    source_id_ = kNullWindowId;
  } else {
    capturer_ = WgcCapturerWin::CreateRawScreenCapturer(
        DesktopCaptureOptions::CreateDefault());
    source_id_ = kInvalidScreenId;
  }

  EXPECT_FALSE(capturer_->SelectSource(source_id_));
}

TEST_P(WgcCapturerWinTest, Capture) {
  if (GetParam() == CaptureType::kWindow) {
    SetUpForWindowCapture();
  } else {
    SetUpForScreenCapture();
  }

  EXPECT_TRUE(capturer_->SelectSource(source_id_));

  capturer_->Start(this);
  EXPECT_GE(metrics::NumEvents(kCapturerImplHistogram,
                               DesktopCapturerId::kWgcCapturerWin),
            1);

  DoCapture();
  EXPECT_GT(frame_->size().width(), 0);
  EXPECT_GT(frame_->size().height(), 0);
}

TEST_P(WgcCapturerWinTest, CaptureTime) {
  if (GetParam() == CaptureType::kWindow) {
    SetUpForWindowCapture();
  } else {
    SetUpForScreenCapture();
  }

  EXPECT_TRUE(capturer_->SelectSource(source_id_));
  capturer_->Start(this);

  int64_t start_time;
  start_time = TimeNanos();
  capturer_->CaptureFrame();

  int capture_time_ms = (TimeNanos() - start_time) / kNumNanosecsPerMillisec;
  EXPECT_EQ(result_, DesktopCapturer::Result::SUCCESS);
  EXPECT_TRUE(frame_);

  // The test may measure the time slightly differently than the capturer. So we
  // just check if it's within 5 ms.
  EXPECT_NEAR(frame_->capture_time_ms(), capture_time_ms, 5);
  EXPECT_GE(
      metrics::NumEvents(kCaptureTimeHistogram, frame_->capture_time_ms()), 1);
}

INSTANTIATE_TEST_SUITE_P(SourceAgnostic,
                         WgcCapturerWinTest,
                         ::testing::Values(CaptureType::kWindow,
                                           CaptureType::kScreen));

TEST(WgcCapturerNoMonitorTest, NoMonitors) {
  ScopedCOMInitializer com_initializer(ScopedCOMInitializer::kMTA);
  EXPECT_TRUE(com_initializer.Succeeded());
  if (HasActiveDisplay()) {
    RTC_LOG(LS_INFO) << "Skip WgcCapturerWinTest designed specifically for "
                        "systems with no monitors";
    GTEST_SKIP();
  }

  // A bug in `CreateForMonitor` prevents screen capture when no displays are
  // attached.
  EXPECT_FALSE(IsWgcSupported(CaptureType::kScreen));

  // A bug in the DWM (Desktop Window Manager) prevents it from providing image
  // data if there are no displays attached. This was fixed in Windows 11.
  if (rtc_win::GetVersion() < rtc_win::Version::VERSION_WIN11)
    EXPECT_FALSE(IsWgcSupported(CaptureType::kWindow));
  else
    EXPECT_TRUE(IsWgcSupported(CaptureType::kWindow));
}

class WgcCapturerMonitorTest : public WgcCapturerWinTest {
 public:
  void SetUp() {
    com_initializer_ =
        std::make_unique<ScopedCOMInitializer>(ScopedCOMInitializer::kMTA);
    EXPECT_TRUE(com_initializer_->Succeeded());

    if (!IsWgcSupported(CaptureType::kScreen)) {
      RTC_LOG(LS_INFO)
          << "Skipping WgcCapturerWinTests on unsupported platforms.";
      GTEST_SKIP();
    }
  }
};

TEST_F(WgcCapturerMonitorTest, FocusOnMonitor) {
  SetUpForScreenCapture();
  EXPECT_TRUE(capturer_->SelectSource(0));

  // You can't set focus on a monitor.
  EXPECT_FALSE(capturer_->FocusOnSelectedSource());
}

TEST_F(WgcCapturerMonitorTest, CaptureAllMonitors) {
  SetUpForScreenCapture();
  EXPECT_TRUE(capturer_->SelectSource(kFullDesktopScreenId));

  capturer_->Start(this);
  DoCapture();
  EXPECT_GT(frame_->size().width(), 0);
  EXPECT_GT(frame_->size().height(), 0);
}

TEST_F(WgcCapturerMonitorTest, CaptureWithValidAdapterLuid) {
  // Get the LUID of the first DXGI adapter on the system.
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  ASSERT_TRUE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  ASSERT_TRUE(SUCCEEDED(factory->EnumAdapters(0, &adapter)));
  DXGI_ADAPTER_DESC desc;
  ASSERT_TRUE(SUCCEEDED(adapter->GetDesc(&desc)));

  LogMessageMatcher fallback_matcher("Failed to get adapter by LUID");
  LogMessage::AddLogToStream(&fallback_matcher, LS_WARNING);

  DesktopCaptureOptions options = DesktopCaptureOptions::CreateDefault();
  options.set_d3d_device_luid(desc.AdapterLuid);
  SetUpForScreenCapture(options);
  EXPECT_TRUE(capturer_->SelectSource(kFullDesktopScreenId));

  capturer_->Start(this);

  LogMessage::RemoveLogToStream(&fallback_matcher);
  EXPECT_FALSE(fallback_matcher.matched());

  DoCapture();
  EXPECT_GT(frame_->size().width(), 0);
  EXPECT_GT(frame_->size().height(), 0);
}

TEST_F(WgcCapturerMonitorTest, CaptureWithInvalidAdapterLuid) {
  // Use a non-zero but invalid LUID. The capturer should fall back to the
  // default adapter and still capture successfully.
  LUID invalid_luid = {0xFFFF, 0x7FFF};

  LogMessageMatcher fallback_matcher("falling back to default adapter");
  LogMessage::AddLogToStream(&fallback_matcher, LS_WARNING);

  DesktopCaptureOptions options = DesktopCaptureOptions::CreateDefault();
  options.set_d3d_device_luid(invalid_luid);
  SetUpForScreenCapture(options);
  EXPECT_TRUE(capturer_->SelectSource(kFullDesktopScreenId));

  capturer_->Start(this);

  LogMessage::RemoveLogToStream(&fallback_matcher);
  EXPECT_TRUE(fallback_matcher.matched());

  DoCapture();
  EXPECT_GT(frame_->size().width(), 0);
  EXPECT_GT(frame_->size().height(), 0);
}

class WgcCapturerWindowTest : public WgcCapturerWinTest {
 public:
  void SetUp() {
    com_initializer_ =
        std::make_unique<ScopedCOMInitializer>(ScopedCOMInitializer::kMTA);
    EXPECT_TRUE(com_initializer_->Succeeded());

    if (!IsWgcSupported(CaptureType::kWindow)) {
      RTC_LOG(LS_INFO)
          << "Skipping WgcCapturerWinTests on unsupported platforms.";
      GTEST_SKIP();
    }
  }

  // Creates a visible WS_CHILD window of the test window. WGC cannot create a
  // GraphicsCaptureItem for it, while GDI can capture it.
  HWND CreateChildWindow() {
    HWND child_hwnd = nullptr;
    SendTask(window_thread_.get(), [this, &child_hwnd]() {
      child_hwnd = ::CreateWindowExW(
          0, kWindowClass, L"Child", WS_CHILD | WS_VISIBLE, 0, 0,
          kSmallWindowWidth, kSmallWindowHeight, window_info_.hwnd, nullptr,
          window_info_.window_instance, nullptr);
    });
    return child_hwnd;
  }

  // Creates window capturers with `options`, which must allow the WGC window
  // capturer and its fallback, and checks SelectSource() and the recorded
  // results for:
  // - A child window: WGC fails, and the fallback capturer succeeds and
  //   captures it into `frame_`.
  // - A minimized window: both fail.
  // - A visible window: both succeed.
  void TestSelectSourceFallback(const DesktopCaptureOptions& options) {
    metrics::Reset();
    SetUpForWindowCapture();

    capturer_ = DesktopCapturer::CreateWindowCapturer(options);
    ASSERT_TRUE(capturer_);
    HWND child_hwnd = CreateChildWindow();
    ASSERT_NE(child_hwnd, nullptr);
    EXPECT_TRUE(capturer_->SelectSource(
        reinterpret_cast<DesktopCapturer::SourceId>(child_hwnd)));
    EXPECT_EQ(NumSelectSourceEvents(
                  SelectSourceResult::kMainFailedSecondarySucceeded),
              1);

    capturer_->Start(this);
    capturer_->CaptureFrame();
    EXPECT_EQ(result_, DesktopCapturer::Result::SUCCESS);
    ASSERT_TRUE(frame_);
    EXPECT_NE(frame_->capturer_id(), DesktopCapturerId::kWgcCapturerWin);

    capturer_ = DesktopCapturer::CreateWindowCapturer(options);
    ASSERT_TRUE(capturer_);
    MinimizeTestWindow(window_info_.hwnd);
    EXPECT_FALSE(capturer_->SelectSource(source_id_));
    EXPECT_EQ(
        NumSelectSourceEvents(SelectSourceResult::kMainFailedSecondaryFailed),
        1);

    UnminimizeTestWindow(window_info_.hwnd);
    capturer_ = DesktopCapturer::CreateWindowCapturer(options);
    ASSERT_TRUE(capturer_);
    EXPECT_TRUE(capturer_->SelectSource(source_id_));
    EXPECT_EQ(NumSelectSourceEvents(
                  SelectSourceResult::kMainSucceededSecondarySucceeded),
              1);
    EXPECT_EQ(metrics::NumSamples(kWgcWindowFallbackSelectSourceHistogram), 3);
  }
};

TEST_F(WgcCapturerWindowTest, FocusOnWindow) {
  capturer_ = WgcCapturerWin::CreateRawWindowCapturer(
      DesktopCaptureOptions::CreateDefault());
  window_info_ = CreateTestWindow(kWindowTitle);
  source_id_ = GetScreenIdFromSourceList();

  EXPECT_TRUE(capturer_->SelectSource(source_id_));
  EXPECT_TRUE(capturer_->FocusOnSelectedSource());

  HWND hwnd = reinterpret_cast<HWND>(source_id_);
  EXPECT_EQ(hwnd, ::GetActiveWindow());
  EXPECT_EQ(hwnd, ::GetForegroundWindow());
  EXPECT_EQ(hwnd, ::GetFocus());
  DestroyTestWindow(window_info_);
}

TEST_F(WgcCapturerWindowTest, SelectMinimizedWindow) {
  SetUpForWindowCapture();
  MinimizeTestWindow(reinterpret_cast<HWND>(source_id_));
  EXPECT_FALSE(capturer_->SelectSource(source_id_));

  UnminimizeTestWindow(reinterpret_cast<HWND>(source_id_));
  EXPECT_TRUE(capturer_->SelectSource(source_id_));
}

TEST_F(WgcCapturerWindowTest, SelectClosedWindow) {
  SetUpForWindowCapture();
  EXPECT_TRUE(capturer_->SelectSource(source_id_));

  CloseTestWindow();
  EXPECT_FALSE(capturer_->SelectSource(source_id_));
}

TEST_F(WgcCapturerWindowTest, UnsupportedWindowStyle) {
  // Create a window with the WS_EX_TOOLWINDOW style, which WGC does not
  // support.
  window_info_ = CreateTestWindow(kWindowTitle, kMediumWindowWidth,
                                  kMediumWindowHeight, WS_EX_TOOLWINDOW);
  capturer_ = WgcCapturerWin::CreateRawWindowCapturer(
      DesktopCaptureOptions::CreateDefault());
  DesktopCapturer::SourceList sources;
  EXPECT_TRUE(capturer_->GetSourceList(&sources));
  auto it = std::find_if(
      sources.begin(), sources.end(), [&](const DesktopCapturer::Source& src) {
        return src.id == reinterpret_cast<intptr_t>(window_info_.hwnd);
      });

  // We should not find the window, since we filter for unsupported styles.
  EXPECT_EQ(it, sources.end());
  DestroyTestWindow(window_info_);
}

TEST_F(WgcCapturerWindowTest, IncreaseWindowSizeMidCapture) {
  SetUpForWindowCapture(kSmallWindowWidth, kSmallWindowHeight);
  EXPECT_TRUE(capturer_->SelectSource(source_id_));

  capturer_->Start(this);
  DoCapture();
  ValidateFrame(kSmallWindowWidth, kSmallWindowHeight);

  ResizeTestWindow(window_info_.hwnd, kSmallWindowWidth, kMediumWindowHeight);
  DoCapture(kNumCapturesToFlushBuffers);
  ValidateFrame(kSmallWindowWidth, kMediumWindowHeight);

  ResizeTestWindow(window_info_.hwnd, kLargeWindowWidth, kMediumWindowHeight);
  DoCapture(kNumCapturesToFlushBuffers);
  ValidateFrame(kLargeWindowWidth, kMediumWindowHeight);
}

TEST_F(WgcCapturerWindowTest, ReduceWindowSizeMidCapture) {
  SetUpForWindowCapture(kLargeWindowWidth, kLargeWindowHeight);
  EXPECT_TRUE(capturer_->SelectSource(source_id_));

  capturer_->Start(this);
  DoCapture();
  ValidateFrame(kLargeWindowWidth, kLargeWindowHeight);

  ResizeTestWindow(window_info_.hwnd, kLargeWindowWidth, kMediumWindowHeight);
  DoCapture(kNumCapturesToFlushBuffers);
  ValidateFrame(kLargeWindowWidth, kMediumWindowHeight);

  ResizeTestWindow(window_info_.hwnd, kSmallWindowWidth, kMediumWindowHeight);
  DoCapture(kNumCapturesToFlushBuffers);
  ValidateFrame(kSmallWindowWidth, kMediumWindowHeight);
}

TEST_F(WgcCapturerWindowTest, MinimizeWindowMidCapture) {
  SetUpForWindowCapture();
  EXPECT_TRUE(capturer_->SelectSource(source_id_));

  capturer_->Start(this);

  // Minmize the window and capture should continue but return temporary errors.
  MinimizeTestWindow(window_info_.hwnd);
  for (int i = 0; i < 5; ++i) {
    capturer_->CaptureFrame();
    EXPECT_EQ(result_, DesktopCapturer::Result::ERROR_TEMPORARY);
  }

  // Reopen the window and the capture should continue normally.
  UnminimizeTestWindow(window_info_.hwnd);
  DoCapture();
  // We can't verify the window size here because the test window does not
  // repaint itself after it is unminimized, but capturing successfully is still
  // a good test.
}

TEST_F(WgcCapturerWindowTest, CloseWindowMidCapture) {
  SetUpForWindowCapture();
  EXPECT_TRUE(capturer_->SelectSource(source_id_));

  capturer_->Start(this);
  DoCapture();
  ValidateFrame(kMediumWindowWidth, kMediumWindowHeight);

  CloseTestWindow();

  // We need to pump our message queue so the Closed event will be delivered to
  // the capturer's event handler. If we are too early and the Closed event
  // hasn't arrived yet we should keep trying until the capturer receives it and
  // stops.
  auto* wgc_capturer = static_cast<WgcCapturerWin*>(capturer_.get());
  MSG msg;
  for (int i = 0;
       wgc_capturer->IsSourceBeingCaptured(source_id_) && i < kMaxTries; ++i) {
    // Unlike GetMessage, PeekMessage will not hang if there are no messages in
    // the queue.
    PeekMessage(&msg, 0, 0, 0, PM_REMOVE);
    Thread::SleepMs(1);
  }

  EXPECT_FALSE(wgc_capturer->IsSourceBeingCaptured(source_id_));

  // The frame pool can buffer `kNumBuffers` frames. We must consume these
  // and then make one more call to CaptureFrame before we expect to see the
  // failure.
  int num_tries = 0;
  do {
    capturer_->CaptureFrame();
  } while (result_ == DesktopCapturer::Result::SUCCESS &&
           ++num_tries <= WgcCaptureSession::kNumBuffers);

  EXPECT_GE(metrics::NumEvents(kCapturerResultHistogram, kSessionStartFailure),
            1);
  EXPECT_GE(metrics::NumEvents(kCaptureSessionResultHistogram, kSourceClosed),
            1);
  EXPECT_EQ(result_, DesktopCapturer::Result::ERROR_PERMANENT);
}

class WgcCapturerFullScreenDetectorTest : public WgcCapturerWindowTest {
 public:
  void SetUp() override {
    DesktopCaptureOptions result;
    result.set_full_screen_window_detector(
        make_ref_counted<FullScreenWindowDetector>(
            [](DesktopCapturer::SourceId sourceId) {
              return std::make_unique<FullScreenPowerPointHandler>(sourceId);
            }));

    capturer_ = WgcCapturerWin::CreateRawWindowCapturer(result);
    wgc_capturer_ = static_cast<WgcCapturerWin*>(capturer_.get());

    editor_window_ = CreateEditorWindow();
    CreateSlideShowWindow();
    WgcCapturerWindowTest::SetUp();
  }

  WindowInfo CreateEditorWindow() {
    return CreateTestWindow(
        L"My - Title - PowerPoint", kMediumWindowHeight, kMediumWindowWidth,
        /*extended_styles=*/0, /*window_class=*/L"PPTFrameClass");
  }

  void CreateSlideShowWindow() {
    slide_show_window_ =
        CreateTestWindow(L"PowerPoint Slide Show - [My - Title]",
                         kLargeWindowHeight, kLargeWindowWidth,
                         /*extended_styles=*/0,
                         /*window_class=*/L"screenClass");
    ResizeTestWindowToFullScreen(slide_show_window_.hwnd);
  }

  WgcCapturerWin* wgc_capturer_;
  WindowInfo editor_window_;
  WindowInfo slide_show_window_;
};

TEST_F(WgcCapturerFullScreenDetectorTest,
       CorrectSlideShowFoundForEditorWhenSlideShowCreatedAfter) {
  wgc_capturer_->SetUpFullScreenDetectorForTest(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd));

  EXPECT_TRUE(wgc_capturer_->SelectSource(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  wgc_capturer_->Start(this);
  DoCapture();

  EXPECT_FALSE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  EXPECT_TRUE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));

  EXPECT_EQ(metrics::NumEvents(kCaptureFullscreenDetectorHistogram, true), 1);
  EXPECT_EQ(
      metrics::NumEvents(kFullScreenDetectorResult, detector_result_success),
      1);
  EXPECT_EQ(metrics::NumEvents(kFullScreenDetectorResult,
                               detector_result_failure_slide_show_not_chosen),
            0);
}

TEST_F(WgcCapturerFullScreenDetectorTest,
       SlideShowNotFoundForEditorWhenSlideShowCreatedBefore) {
  wgc_capturer_->SetUpFullScreenDetectorForTest(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd),
      /*fullscreen_slide_show_started_after_capture_start=*/false);

  EXPECT_TRUE(wgc_capturer_->SelectSource(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  wgc_capturer_->Start(this);
  DoCapture();

  EXPECT_TRUE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  EXPECT_FALSE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));

  EXPECT_EQ(metrics::NumEvents(kCaptureFullscreenDetectorHistogram, true), 0);
  EXPECT_EQ(
      metrics::NumEvents(kFullScreenDetectorResult, detector_result_success),
      0);
  EXPECT_EQ(metrics::NumEvents(kFullScreenDetectorResult,
                               detector_result_failure_slide_show_not_chosen),
            1);
}

TEST_F(WgcCapturerFullScreenDetectorTest, EditorFoundForSlideShow) {
  wgc_capturer_->SetUpFullScreenDetectorForTest(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd),
      /*fullscreen_slide_show_started_after_capture_start=*/false);

  EXPECT_TRUE(wgc_capturer_->SelectSource(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));
  wgc_capturer_->Start(this);

  // Call DoCapture() multiple times to update the window list and to allow
  // finding the editor.
  DoCapture();
  DoCapture();
  DoCapture();

  EXPECT_FALSE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  EXPECT_TRUE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));

  EXPECT_EQ(wgc_capturer_->SelectedSourceId(),
            reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd));
}

TEST_F(WgcCapturerFullScreenDetectorTest, LoggedOnlyOnce) {
  wgc_capturer_->SetUpFullScreenDetectorForTest(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd));

  EXPECT_TRUE(wgc_capturer_->SelectSource(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  wgc_capturer_->Start(this);
  DoCapture();
  DoCapture();

  EXPECT_TRUE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));
  EXPECT_EQ(metrics::NumEvents(kCaptureFullscreenDetectorHistogram, true), 1);
  EXPECT_EQ(
      metrics::NumEvents(kFullScreenDetectorResult, detector_result_success),
      1);
}

TEST_F(WgcCapturerFullScreenDetectorTest,
       SlideShowNotFoundWithMultipleSameTitleEditors) {
  WindowInfo same_title_editor_window = CreateEditorWindow();
  EXPECT_NE(editor_window_.hwnd, same_title_editor_window.hwnd);
  wgc_capturer_->SetUpFullScreenDetectorForTest(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd));

  EXPECT_TRUE(wgc_capturer_->SelectSource(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  wgc_capturer_->Start(this);
  DoCapture();

  EXPECT_TRUE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  EXPECT_FALSE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));
  EXPECT_EQ(metrics::NumEvents(kCaptureFullscreenDetectorHistogram, true), 0);
  EXPECT_EQ(
      metrics::NumEvents(kFullScreenDetectorResult, detector_result_success),
      0);
  EXPECT_EQ(metrics::NumEvents(kFullScreenDetectorResult,
                               detector_result_failure_same_title_windows),
            1);
}

TEST_F(WgcCapturerFullScreenDetectorTest,
       CaptureTiedToSlideShowIfSlideShowIsShared) {
  wgc_capturer_->SetUpFullScreenDetectorForTest(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd));
  wgc_capturer_->SetUpFullScreenDetectorForTest(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd));

  EXPECT_TRUE(wgc_capturer_->SelectSource(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));
  wgc_capturer_->Start(this);
  DoCapture();

  EXPECT_FALSE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(editor_window_.hwnd)));
  EXPECT_TRUE(wgc_capturer_->IsSourceBeingCaptured(
      reinterpret_cast<DesktopCapturer::SourceId>(slide_show_window_.hwnd)));
  EXPECT_EQ(metrics::NumEvents(kCaptureFullscreenDetectorHistogram, true), 0);
  EXPECT_EQ(
      metrics::NumEvents(kFullScreenDetectorResult, detector_result_success),
      0);
  EXPECT_EQ(metrics::NumEvents(kFullScreenDetectorResult,
                               detector_result_failure_slide_show_not_chosen),
            0);
}

TEST_F(WgcCapturerWindowTest,
       CreateWindowCapturerFallsBackToGdiOnSelectSourceFailure) {
  DesktopCaptureOptions options = DesktopCaptureOptions::CreateDefault();
  options.set_allow_wgc_window_capturer(true);
  options.set_allow_fallback_for_wgc_window_capturer(true);
  ASSERT_NO_FATAL_FAILURE(TestSelectSourceFallback(options));

  // Without cropping, the fallback capturer is WindowCapturerWinGdi.
  EXPECT_EQ(frame_->capturer_id(), DesktopCapturerId::kWindowCapturerWinGdi);
}

TEST_F(WgcCapturerWindowTest,
       CreateWindowCapturerFallsBackToCroppingCapturerOnSelectSourceFailure) {
  DesktopCaptureOptions options = DesktopCaptureOptions::CreateDefault();
  options.set_allow_wgc_window_capturer(true);
  options.set_allow_fallback_for_wgc_window_capturer(true);
  options.set_allow_cropping_window_capturer(true);
  // Chrome also allows the GDI window capturer's own WGC fallback, which on
  // Windows 11 accepts any window. The cropping fallback capturer must not use
  // it, or the minimized window would be accepted.
  options.set_allow_wgc_capturer_fallback(true);
  TestSelectSourceFallback(options);
}

TEST_F(WgcCapturerWindowTest, CreateWindowCapturerWithoutFallback) {
  metrics::Reset();
  SetUpForWindowCapture();

  DesktopCaptureOptions options = DesktopCaptureOptions::CreateDefault();
  options.set_allow_wgc_window_capturer(true);
  capturer_ = DesktopCapturer::CreateWindowCapturer(options);
  ASSERT_TRUE(capturer_);

  // Without the fallback, WGC's SelectSource failure is returned as is.
  HWND child_hwnd = CreateChildWindow();
  ASSERT_NE(child_hwnd, nullptr);

  EXPECT_FALSE(capturer_->SelectSource(
      reinterpret_cast<DesktopCapturer::SourceId>(child_hwnd)));
  EXPECT_EQ(metrics::NumSamples(kWgcWindowFallbackSelectSourceHistogram), 0);
}

// Tests for the WGC texture mode (allow_wgc_using_texture).
class WgcCapturerTextureTest : public ::testing::TestWithParam<CaptureType>,
                               public DesktopCapturer::Callback {
 public:
  void SetUp() override {
    com_initializer_ =
        std::make_unique<ScopedCOMInitializer>(ScopedCOMInitializer::kMTA);
    ASSERT_TRUE(com_initializer_->Succeeded());

    if (!IsWgcSupported(GetParam())) {
      RTC_LOG(LS_INFO)
          << "Skipping WgcCapturerTextureTests on unsupported platforms.";
      GTEST_SKIP();
    }
  }

  void SetUpForWindowCapture(int window_width = kMediumWindowWidth,
                             int window_height = kMediumWindowHeight) {
    auto options = DesktopCaptureOptions::CreateDefault();
    options.set_allow_wgc_using_texture(true);
    capturer_ = WgcCapturerWin::CreateRawWindowCapturer(options);
    CreateWindowOnSeparateThread(window_width, window_height);
    StartWindowThreadMessageLoop();
    source_id_ = GetTestWindowIdFromSourceList();
  }

  void SetUpForScreenCapture() {
    auto options = DesktopCaptureOptions::CreateDefault();
    options.set_allow_wgc_using_texture(true);
    capturer_ = WgcCapturerWin::CreateRawScreenCapturer(options);
    source_id_ = GetScreenIdFromSourceList();
  }

  void TearDown() override {
    if (window_open_) {
      CloseTestWindow();
    }
  }

  void CreateWindowOnSeparateThread(int window_width, int window_height) {
    window_thread_ = Thread::Create();
    window_thread_->SetName(kWindowThreadName, nullptr);
    window_thread_->Start();
    SendTask(window_thread_.get(), [this, window_width, window_height]() {
      window_thread_id_ = GetCurrentThreadId();
      window_info_ =
          CreateTestWindow(kWindowTitle, window_height, window_width);
      window_open_ = true;

      while (!IsWindowResponding(window_info_.hwnd)) {
        RTC_LOG(LS_INFO) << "Waiting for test window to become responsive in "
                            "WgcCapturerTextureTest.";
      }

      while (!IsWindowValidAndVisible(window_info_.hwnd)) {
        RTC_LOG(LS_INFO) << "Waiting for test window to be visible in "
                            "WgcCapturerTextureTest.";
      }
    });

    ASSERT_TRUE(window_thread_->RunningForTest());
    ASSERT_FALSE(window_thread_->IsCurrent());
  }

  void StartWindowThreadMessageLoop() {
    window_thread_->PostTask([this]() {
      MSG msg;
      BOOL gm;
      while ((gm = ::GetMessage(&msg, NULL, 0, 0)) != 0 && gm != -1) {
        ::DispatchMessage(&msg);
        if (msg.message == kDestroyWindow) {
          DestroyTestWindow(window_info_);
        }
        if (msg.message == kQuitRunning) {
          PostQuitMessage(0);
        }
      }
    });
  }

  void CloseTestWindow() {
    ::PostThreadMessage(window_thread_id_, kDestroyWindow, 0, 0);
    ::PostThreadMessage(window_thread_id_, kQuitRunning, 0, 0);
    window_thread_->Stop();
    window_open_ = false;
  }

  DesktopCapturer::SourceId GetTestWindowIdFromSourceList() {
    intptr_t src_id = 0;
    do {
      DesktopCapturer::SourceList sources;
      EXPECT_TRUE(capturer_->GetSourceList(&sources));
      auto it = std::find_if(
          sources.begin(), sources.end(),
          [&](const DesktopCapturer::Source& src) {
            return src.id == reinterpret_cast<intptr_t>(window_info_.hwnd);
          });
      if (it != sources.end())
        src_id = it->id;
    } while (src_id != reinterpret_cast<intptr_t>(window_info_.hwnd));
    return src_id;
  }

  DesktopCapturer::SourceId GetScreenIdFromSourceList() {
    DesktopCapturer::SourceList sources;
    EXPECT_TRUE(capturer_->GetSourceList(&sources));
    EXPECT_GT(sources.size(), 0ULL);
    return sources[0].id;
  }

  void DoCapture(int num_captures = 1) {
    const int max_tries = num_captures == 1 ? 1 : kMaxTries;
    int success_count = 0;
    for (int i = 0; success_count < num_captures && i < max_tries; i++) {
      capturer_->CaptureFrame();
      if (result_ == DesktopCapturer::Result::ERROR_PERMANENT)
        break;
      if (result_ == DesktopCapturer::Result::SUCCESS)
        success_count++;
    }

    total_successful_captures_ += success_count;
    EXPECT_EQ(success_count, num_captures);
    EXPECT_EQ(result_, DesktopCapturer::Result::SUCCESS);
    EXPECT_TRUE(frame_);
  }

  // DesktopCapturer::Callback interface
  void OnCaptureResult(DesktopCapturer::Result result,
                       std::unique_ptr<DesktopFrame> frame) override {
    result_ = result;
    frame_ = std::move(frame);
  }

 protected:
  std::unique_ptr<ScopedCOMInitializer> com_initializer_;
  DWORD window_thread_id_;
  std::unique_ptr<Thread> window_thread_;
  WindowInfo window_info_;
  intptr_t source_id_;
  bool window_open_ = false;
  DesktopCapturer::Result result_;
  int total_successful_captures_ = 0;
  std::unique_ptr<DesktopFrame> frame_;
  std::unique_ptr<DesktopCapturer> capturer_;
};

// Verifies that the captured frame in texture mode has a non-null texture with
// a valid handle, and that the CPU data pointer is null (texture-only frame).
TEST_P(WgcCapturerTextureTest, CapturedFrameHasTexture) {
  if (GetParam() == CaptureType::kWindow) {
    SetUpForWindowCapture();
  } else {
    SetUpForScreenCapture();
  }

  EXPECT_TRUE(capturer_->SelectSource(source_id_));
  capturer_->Start(this);
  DoCapture();

  ASSERT_NE(frame_, nullptr);
  EXPECT_GT(frame_->size().width(), 0);
  EXPECT_GT(frame_->size().height(), 0);
  ASSERT_NE(frame_->texture(), nullptr);
  EXPECT_NE(frame_->texture()->handle(), FrameTexture::kInvalidHandle);
  // In texture mode, frame data should be null (no CPU copy).
  EXPECT_EQ(frame_->data(), nullptr);
}

// Verifies that consecutive captures in texture mode each produce frames with
// valid texture handles.
TEST_P(WgcCapturerTextureTest, MultipleCapturesProduceValidTextures) {
  if (GetParam() == CaptureType::kWindow) {
    SetUpForWindowCapture();
  } else {
    SetUpForScreenCapture();
  }

  EXPECT_TRUE(capturer_->SelectSource(source_id_));
  capturer_->Start(this);

  for (int i = 0; i < 3; ++i) {
    DoCapture();
    ASSERT_NE(frame_, nullptr);
    ASSERT_NE(frame_->texture(), nullptr);
    EXPECT_NE(frame_->texture()->handle(), FrameTexture::kInvalidHandle);
  }
}

// Verifies that the frame pool uses `kNumBuffersForTexture` buffers when
// texture mode is enabled (verified indirectly by successfully capturing more
// frames than kNumBuffers without starvation).
TEST_P(WgcCapturerTextureTest, CaptureDoesNotStarveWithTextureBuffers) {
  if (GetParam() == CaptureType::kWindow) {
    SetUpForWindowCapture();
  } else {
    SetUpForScreenCapture();
  }

  EXPECT_TRUE(capturer_->SelectSource(source_id_));
  capturer_->Start(this);

  // Capture more frames than kNumBuffers to ensure the texture buffer count
  // (kNumBuffersForTexture) is sufficient.
  constexpr int kCaptures = WgcCaptureSession::kNumBuffersForTexture + 1;
  for (int i = 0; i < kCaptures; ++i) {
    DoCapture();
    ASSERT_NE(frame_, nullptr);
    ASSERT_NE(frame_->texture(), nullptr);
  }
}

INSTANTIATE_TEST_SUITE_P(SourceAgnosticTexture,
                         WgcCapturerTextureTest,
                         ::testing::Values(CaptureType::kWindow,
                                           CaptureType::kScreen));

// Window-specific texture mode tests.
class WgcCapturerTextureWindowTest : public WgcCapturerTextureTest {
 public:
  void SetUp() override {
    com_initializer_ =
        std::make_unique<ScopedCOMInitializer>(ScopedCOMInitializer::kMTA);
    ASSERT_TRUE(com_initializer_->Succeeded());

    if (!IsWgcSupported(CaptureType::kWindow)) {
      RTC_LOG(LS_INFO) << "Skipping WgcCapturerTextureWindowTest on "
                          "unsupported platforms.";
      GTEST_SKIP();
    }
  }
};

// Verifies that the cursor is embedded in the frame when texture mode is
// enabled (since we cannot do cursor composition on texture).
TEST_F(WgcCapturerTextureWindowTest, CursorEmbeddedInTextureMode) {
  SetUpForWindowCapture();
  EXPECT_TRUE(capturer_->SelectSource(source_id_));
  capturer_->Start(this);
  DoCapture();
  ASSERT_NE(frame_, nullptr);
  // When texture mode is enabled, cursor embedding should be forced on,
  // so the frame should indicate it may contain the cursor.
  EXPECT_TRUE(frame_->may_contain_cursor());
}

// Verifies capture continues to work after a window resize in texture mode.
TEST_F(WgcCapturerTextureWindowTest, ResizeWindowMidTextureCapture) {
  SetUpForWindowCapture(kSmallWindowWidth, kSmallWindowHeight);
  EXPECT_TRUE(capturer_->SelectSource(source_id_));
  capturer_->Start(this);
  DoCapture();
  ASSERT_NE(frame_, nullptr);
  ASSERT_NE(frame_->texture(), nullptr);

  // Resize the window.
  ResizeTestWindow(window_info_.hwnd, kLargeWindowWidth, kLargeWindowHeight);

  // We need more captures to flush the buffers and pick up the new size.
  constexpr int kNumCapturesToFlushTextureBuffers =
      WgcCaptureSession::kNumBuffersForTexture * 2 + 1;
  DoCapture(kNumCapturesToFlushTextureBuffers);
  ASSERT_NE(frame_, nullptr);
  ASSERT_NE(frame_->texture(), nullptr);
  EXPECT_NE(frame_->texture()->handle(), FrameTexture::kInvalidHandle);
  EXPECT_GT(frame_->size().width(), 0);
  EXPECT_GT(frame_->size().height(), 0);
}

}  // namespace webrtc
