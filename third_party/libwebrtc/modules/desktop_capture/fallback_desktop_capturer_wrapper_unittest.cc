/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/fallback_desktop_capturer_wrapper.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/desktop_frame_generator.h"
#include "modules/desktop_capture/fake_desktop_capturer.h"
#include "modules/desktop_capture/shared_memory.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

std::unique_ptr<DesktopCapturer> CreateDesktopCapturer(
    PainterDesktopFrameGenerator* frame_generator) {
  std::unique_ptr<FakeDesktopCapturer> capturer(new FakeDesktopCapturer());
  capturer->set_frame_generator(frame_generator);
  return std::move(capturer);
}

class FakeSharedMemory : public SharedMemory {
 public:
  explicit FakeSharedMemory(size_t size);
  ~FakeSharedMemory() override;

 private:
  static int next_id_;
};

// static
int FakeSharedMemory::next_id_ = 0;

FakeSharedMemory::FakeSharedMemory(size_t size)
    : SharedMemory(new char[size], size, 0, next_id_++) {}

FakeSharedMemory::~FakeSharedMemory() {
  delete[] static_cast<char*>(data_);
}

class FakeSharedMemoryFactory : public SharedMemoryFactory {
 public:
  FakeSharedMemoryFactory() = default;
  ~FakeSharedMemoryFactory() override = default;

  std::unique_ptr<SharedMemory> CreateSharedMemory(size_t size) override;
};

std::unique_ptr<SharedMemory> FakeSharedMemoryFactory::CreateSharedMemory(
    size_t size) {
  return std::unique_ptr<SharedMemory>(new FakeSharedMemory(size));
}

class SelectSourceFakeCapturer : public FakeDesktopCapturer {
 public:
  void set_select_source_result(bool result) { select_source_result_ = result; }

  bool SelectSource(DesktopCapturer::SourceId id) override {
    last_selected_source_ = id;
    return select_source_result_;
  }

  std::optional<DesktopCapturer::SourceId> last_selected_source() const {
    return last_selected_source_;
  }

 private:
  bool select_source_result_ = true;
  std::optional<DesktopCapturer::SourceId> last_selected_source_;
};

}  // namespace

class FallbackDesktopCapturerWrapperTest : public ::testing::Test,
                                           public DesktopCapturer::Callback {
 public:
  FallbackDesktopCapturerWrapperTest();
  ~FallbackDesktopCapturerWrapperTest() override = default;

 protected:
  std::vector<std::pair<DesktopCapturer::Result, bool>> results_;
  FakeDesktopCapturer* main_capturer_ = nullptr;
  FakeDesktopCapturer* secondary_capturer_ = nullptr;
  std::unique_ptr<FallbackDesktopCapturerWrapper> wrapper_;

 private:
  // DesktopCapturer::Callback interface
  void OnCaptureResult(DesktopCapturer::Result result,
                       std::unique_ptr<DesktopFrame> frame) override;
  PainterDesktopFrameGenerator frame_generator_;
};

FallbackDesktopCapturerWrapperTest::FallbackDesktopCapturerWrapperTest() {
  frame_generator_.size()->set(1024, 768);
  std::unique_ptr<DesktopCapturer> main_capturer =
      CreateDesktopCapturer(&frame_generator_);
  std::unique_ptr<DesktopCapturer> secondary_capturer =
      CreateDesktopCapturer(&frame_generator_);
  main_capturer_ = static_cast<FakeDesktopCapturer*>(main_capturer.get());
  secondary_capturer_ =
      static_cast<FakeDesktopCapturer*>(secondary_capturer.get());
  wrapper_.reset(new FallbackDesktopCapturerWrapper(
      std::move(main_capturer), std::move(secondary_capturer)));
  wrapper_->Start(this);
}

void FallbackDesktopCapturerWrapperTest::OnCaptureResult(
    DesktopCapturer::Result result,
    std::unique_ptr<DesktopFrame> frame) {
  results_.emplace_back(result, !!frame);
}

TEST_F(FallbackDesktopCapturerWrapperTest, MainNeverFailed) {
  wrapper_->CaptureFrame();
  ASSERT_EQ(main_capturer_->num_capture_attempts(), 1);
  ASSERT_EQ(main_capturer_->num_frames_captured(), 1);
  ASSERT_EQ(secondary_capturer_->num_capture_attempts(), 0);
  ASSERT_EQ(secondary_capturer_->num_frames_captured(), 0);
  ASSERT_EQ(results_.size(), 1U);
  ASSERT_EQ(results_[0],
            std::make_pair(DesktopCapturer::Result::SUCCESS, true));
}

TEST_F(FallbackDesktopCapturerWrapperTest, MainFailedTemporarily) {
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::ERROR_TEMPORARY);
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::SUCCESS);
  wrapper_->CaptureFrame();

  ASSERT_EQ(main_capturer_->num_capture_attempts(), 3);
  ASSERT_EQ(main_capturer_->num_frames_captured(), 2);
  ASSERT_EQ(secondary_capturer_->num_capture_attempts(), 1);
  ASSERT_EQ(secondary_capturer_->num_frames_captured(), 1);
  ASSERT_EQ(results_.size(), 3U);
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(results_[i],
              std::make_pair(DesktopCapturer::Result::SUCCESS, true));
  }
}

TEST_F(FallbackDesktopCapturerWrapperTest, MainFailedPermanently) {
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::ERROR_PERMANENT);
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::SUCCESS);
  wrapper_->CaptureFrame();

  ASSERT_EQ(main_capturer_->num_capture_attempts(), 2);
  ASSERT_EQ(main_capturer_->num_frames_captured(), 1);
  ASSERT_EQ(secondary_capturer_->num_capture_attempts(), 2);
  ASSERT_EQ(secondary_capturer_->num_frames_captured(), 2);
  ASSERT_EQ(results_.size(), 3U);
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(results_[i],
              std::make_pair(DesktopCapturer::Result::SUCCESS, true));
  }
}

TEST_F(FallbackDesktopCapturerWrapperTest, BothFailed) {
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::ERROR_PERMANENT);
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::SUCCESS);
  wrapper_->CaptureFrame();
  secondary_capturer_->set_result(DesktopCapturer::Result::ERROR_TEMPORARY);
  wrapper_->CaptureFrame();
  secondary_capturer_->set_result(DesktopCapturer::Result::ERROR_PERMANENT);
  wrapper_->CaptureFrame();
  wrapper_->CaptureFrame();

  ASSERT_EQ(main_capturer_->num_capture_attempts(), 2);
  ASSERT_EQ(main_capturer_->num_frames_captured(), 1);
  ASSERT_EQ(secondary_capturer_->num_capture_attempts(), 5);
  ASSERT_EQ(secondary_capturer_->num_frames_captured(), 2);
  ASSERT_EQ(results_.size(), 6U);
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(results_[i],
              std::make_pair(DesktopCapturer::Result::SUCCESS, true));
  }
  ASSERT_EQ(results_[3],
            std::make_pair(DesktopCapturer::Result::ERROR_TEMPORARY, false));
  ASSERT_EQ(results_[4],
            std::make_pair(DesktopCapturer::Result::ERROR_PERMANENT, false));
  ASSERT_EQ(results_[5],
            std::make_pair(DesktopCapturer::Result::ERROR_PERMANENT, false));
}

TEST_F(FallbackDesktopCapturerWrapperTest, WithSharedMemory) {
  wrapper_->SetSharedMemoryFactory(
      std::unique_ptr<SharedMemoryFactory>(new FakeSharedMemoryFactory()));
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::ERROR_TEMPORARY);
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::SUCCESS);
  wrapper_->CaptureFrame();
  main_capturer_->set_result(DesktopCapturer::Result::ERROR_PERMANENT);
  wrapper_->CaptureFrame();
  wrapper_->CaptureFrame();

  ASSERT_EQ(main_capturer_->num_capture_attempts(), 4);
  ASSERT_EQ(main_capturer_->num_frames_captured(), 2);
  ASSERT_EQ(secondary_capturer_->num_capture_attempts(), 3);
  ASSERT_EQ(secondary_capturer_->num_frames_captured(), 3);
  ASSERT_EQ(results_.size(), 5U);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(results_[i],
              std::make_pair(DesktopCapturer::Result::SUCCESS, true));
  }
}

class FallbackDesktopCapturerWrapperSelectSourceTest : public ::testing::Test {
 protected:
  // Creates `wrapper_` with fake capturers whose SelectSource() returns
  // `main_result` and `secondary_result`.
  void CreateWrapper(bool main_result, bool secondary_result) {
    auto main = std::make_unique<SelectSourceFakeCapturer>();
    auto secondary = std::make_unique<SelectSourceFakeCapturer>();
    main->set_select_source_result(main_result);
    secondary->set_select_source_result(secondary_result);
    main_ = main.get();
    secondary_ = secondary.get();
    wrapper_ = std::make_unique<FallbackDesktopCapturerWrapper>(
        std::move(main), std::move(secondary), "WebRTC.Test.SelectSource");
  }

  SelectSourceFakeCapturer* main_ = nullptr;
  SelectSourceFakeCapturer* secondary_ = nullptr;
  std::unique_ptr<FallbackDesktopCapturerWrapper> wrapper_;
};

TEST_F(FallbackDesktopCapturerWrapperSelectSourceTest, BothSucceed) {
  CreateWrapper(/*main_result=*/true, /*secondary_result=*/true);

  EXPECT_TRUE(wrapper_->SelectSource(123));
  EXPECT_EQ(main_->last_selected_source(), 123);
  EXPECT_EQ(secondary_->last_selected_source(), 123);
}

TEST_F(FallbackDesktopCapturerWrapperSelectSourceTest,
       MainSucceedsSecondaryFails) {
  CreateWrapper(/*main_result=*/true, /*secondary_result=*/false);

  // The wrapper returns the secondary capturer's result.
  EXPECT_FALSE(wrapper_->SelectSource(123));
  EXPECT_EQ(main_->last_selected_source(), 123);
  EXPECT_EQ(secondary_->last_selected_source(), 123);
}

TEST_F(FallbackDesktopCapturerWrapperSelectSourceTest,
       MainFailsSecondarySucceeds) {
  CreateWrapper(/*main_result=*/false, /*secondary_result=*/true);

  EXPECT_TRUE(wrapper_->SelectSource(123));
  EXPECT_EQ(main_->last_selected_source(), 123);
  EXPECT_EQ(secondary_->last_selected_source(), 123);

  // Subsequent SelectSource calls do not invoke the main capturer again.
  main_->set_select_source_result(true);
  EXPECT_TRUE(wrapper_->SelectSource(456));
  EXPECT_EQ(main_->last_selected_source(), 123);
  EXPECT_EQ(secondary_->last_selected_source(), 456);
}

TEST_F(FallbackDesktopCapturerWrapperSelectSourceTest, BothFail) {
  CreateWrapper(/*main_result=*/false, /*secondary_result=*/false);

  EXPECT_FALSE(wrapper_->SelectSource(123));
  EXPECT_EQ(main_->last_selected_source(), 123);
  EXPECT_EQ(secondary_->last_selected_source(), 123);
}

}  // namespace webrtc
