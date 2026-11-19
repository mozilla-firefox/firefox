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
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "api/sequence_checker.h"
#include "modules/desktop_capture/desktop_capture_types.h"
#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/desktop_frame.h"
#include "modules/desktop_capture/desktop_geometry.h"
#include "modules/desktop_capture/shared_memory.h"
#include "rtc_base/checks.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {

namespace {

// Implementation to share a SharedMemoryFactory between DesktopCapturer
// instances. This class is designed for synchronized DesktopCapturer
// implementations only.
class SharedMemoryFactoryProxy : public SharedMemoryFactory {
 public:
  // Users should maintain the lifetime of `factory` to ensure it overlives
  // current instance.
  static std::unique_ptr<SharedMemoryFactory> Create(
      SharedMemoryFactory* factory);
  ~SharedMemoryFactoryProxy() override;

  // Forwards CreateSharedMemory() calls to `factory_`. Users should always call
  // this function in one thread. Users should not call this function after the
  // SharedMemoryFactory which current instance created from has been destroyed.
  std::unique_ptr<SharedMemory> CreateSharedMemory(size_t size) override;

 private:
  explicit SharedMemoryFactoryProxy(SharedMemoryFactory* factory);

  SharedMemoryFactory* factory_ = nullptr;
  SequenceChecker thread_checker_;
};

}  // namespace

SharedMemoryFactoryProxy::SharedMemoryFactoryProxy(
    SharedMemoryFactory* factory) {
  RTC_DCHECK(factory);
  factory_ = factory;
}

// static
std::unique_ptr<SharedMemoryFactory> SharedMemoryFactoryProxy::Create(
    SharedMemoryFactory* factory) {
  return std::unique_ptr<SharedMemoryFactory>(
      new SharedMemoryFactoryProxy(factory));
}

SharedMemoryFactoryProxy::~SharedMemoryFactoryProxy() = default;

std::unique_ptr<SharedMemory> SharedMemoryFactoryProxy::CreateSharedMemory(
    size_t size) {
  RTC_DCHECK(thread_checker_.IsCurrent());
  return factory_->CreateSharedMemory(size);
}

// Observes `main_capturer_` and intercepts `secondary_capturer_` callbacks to
// record at most one StartOutcome to `histogram_name_`:
//   1. FallbackDesktopCapturerWrapper::Start() calls
//      OutcomeRecorder::SetCallback() and passes this instance to
//      secondary_capturer_->Start().
//   2. FallbackDesktopCapturerWrapper::SelectSource() calls
//      OutcomeRecorder::OnSelectSourceResult(). Records immediately if
//      `secondary_capturer_` rejected the source; otherwise, if
//      `main_capturer_` rejected it, enters
//      kWaitingForSecondaryAfterMainRejected.
//   3. FallbackDesktopCapturerWrapper::OnCaptureResult() calls
//      OutcomeRecorder::OnMainCaptureResult() for `main_capturer_`, while
//      `secondary_capturer_` calls OutcomeRecorder::OnCaptureResult().
//   4. ~OutcomeRecorder() records failure if fallback started and every
//      secondary_capturer_->CaptureFrame() call returned ERROR_TEMPORARY.
class FallbackDesktopCapturerWrapper::OutcomeRecorder final
    : public DesktopCapturer::Callback {
 public:
  static std::unique_ptr<OutcomeRecorder> Create(
      absl::string_view histogram_name) {
    if (histogram_name.empty()) {
      return nullptr;
    }
    return std::unique_ptr<OutcomeRecorder>(
        new OutcomeRecorder(histogram_name));
  }

  OutcomeRecorder(const OutcomeRecorder&) = delete;
  OutcomeRecorder& operator=(const OutcomeRecorder&) = delete;

  ~OutcomeRecorder() override {
    if (state_ != State::kRecorded && secondary_returned_temporary_error_) {
      RecordSecondaryOutcome(/*secondary_worked=*/false);
    }
  }

  void SetCallback(DesktopCapturer::Callback* callback) {
    callback_ = callback;
  }

  void OnSelectSourceResult(bool main_accepted, bool secondary_accepted) {
    if (state_ == State::kRecorded) {
      return;
    }
    if (!secondary_accepted) {
      RecordStartOutcome(main_accepted
                             ? StartOutcome::kMainAcceptedSecondaryRejected
                             : StartOutcome::kMainRejectedSecondaryRejected);
      return;
    }
    if (!main_accepted) {
      state_ = State::kWaitingForSecondaryAfterMainRejected;
    }
  }

  // Called by FallbackDesktopCapturerWrapper::OnCaptureResult() for
  // `main_capturer_`.
  void OnMainCaptureResult(Result result) {
    if (state_ != State::kWaitingForMainFirstFrame) {
      // Ignore results once a start outcome has been recorded (including
      // mid-capture results after the first frame) or after `main_capturer_`
      // already rejected the source or failed permanently.
      // TODO(crbug.com/567859840): Record mid-capture fallback outcomes.
      return;
    }
    if (result == Result::SUCCESS) {
      RecordStartOutcome(StartOutcome::kMainWorked);
    } else if (result == Result::ERROR_PERMANENT) {
      state_ = State::kWaitingForSecondaryAfterMainStartError;
    }
  }

  // DesktopCapturer::Callback interface, passed to `secondary_capturer_`.
  void OnFrameCaptureStart() override {
    if (callback_) {
      callback_->OnFrameCaptureStart();
    }
  }

  void OnCaptureResult(Result result,
                       std::unique_ptr<DesktopFrame> frame) override {
    // Only record when permanently using `secondary_capturer_` and no outcome
    // has been recorded yet. When `main_capturer_` returns ERROR_TEMPORARY,
    // FallbackDesktopCapturerWrapper::OnCaptureResult() calls
    // secondary_capturer_->CaptureFrame() for that call while `state_` is
    // still kWaitingForMainFirstFrame.
    if (state_ == State::kWaitingForSecondaryAfterMainRejected ||
        state_ == State::kWaitingForSecondaryAfterMainStartError) {
      if (result == Result::ERROR_TEMPORARY) {
        // Don't record yet: a later secondary_capturer_->CaptureFrame() call
        // may still succeed. Failure is recorded in ~OutcomeRecorder() if no
        // later call succeeds or fails permanently.
        secondary_returned_temporary_error_ = true;
      } else {
        RecordSecondaryOutcome(/*secondary_worked=*/result == Result::SUCCESS);
      }
    }
    if (callback_) {
      callback_->OnCaptureResult(result, std::move(frame));
    }
  }

 private:
  enum class State {
    // `main_capturer_` is active and has not delivered its first frame yet.
    kWaitingForMainFirstFrame,
    // main_capturer_->SelectSource() rejected the source; waiting for
    // `secondary_capturer_` to deliver a frame or fail.
    kWaitingForSecondaryAfterMainRejected,
    // `main_capturer_` returned ERROR_PERMANENT before delivering a frame;
    // waiting for `secondary_capturer_` to deliver a frame or fail.
    kWaitingForSecondaryAfterMainStartError,
    // StartOutcome has already been recorded.
    kRecorded,
  };

  explicit OutcomeRecorder(absl::string_view histogram_name)
      : histogram_name_(histogram_name) {}

  void RecordSecondaryOutcome(bool secondary_worked) {
    if (state_ == State::kWaitingForSecondaryAfterMainRejected) {
      RecordStartOutcome(secondary_worked
                             ? StartOutcome::kMainRejectedSecondaryWorked
                             : StartOutcome::kMainRejectedSecondaryFailed);
    } else if (state_ == State::kWaitingForSecondaryAfterMainStartError) {
      RecordStartOutcome(secondary_worked
                             ? StartOutcome::kMainStartErrorSecondaryWorked
                             : StartOutcome::kMainStartErrorSecondaryFailed);
    }
  }

  void RecordStartOutcome(StartOutcome outcome) {
    if (state_ == State::kRecorded) {
      return;
    }
    state_ = State::kRecorded;
    RTC_HISTOGRAM_ENUMERATION(histogram_name_, static_cast<int>(outcome),
                              static_cast<int>(StartOutcome::kMaxValue) + 1);
  }

  const std::string histogram_name_;
  // The callback passed to FallbackDesktopCapturerWrapper::Start(), to which
  // `secondary_capturer_` callbacks are forwarded.
  DesktopCapturer::Callback* callback_ = nullptr;
  State state_ = State::kWaitingForMainFirstFrame;
  bool secondary_returned_temporary_error_ = false;
};

FallbackDesktopCapturerWrapper::FallbackDesktopCapturerWrapper(
    std::unique_ptr<DesktopCapturer> main_capturer,
    std::unique_ptr<DesktopCapturer> secondary_capturer,
    absl::string_view start_histogram)
    : outcome_recorder_(OutcomeRecorder::Create(start_histogram)),
      main_capturer_(std::move(main_capturer)),
      secondary_capturer_(std::move(secondary_capturer)) {
  RTC_DCHECK(main_capturer_);
  RTC_DCHECK(secondary_capturer_);
}

FallbackDesktopCapturerWrapper::~FallbackDesktopCapturerWrapper() = default;

void FallbackDesktopCapturerWrapper::Start(
    DesktopCapturer::Callback* callback) {
  callback_ = callback;
  // FallbackDesktopCapturerWrapper catchs the callback of the main capturer,
  // and checks its return value to decide whether the secondary capturer should
  // be involved.
  main_capturer_->Start(this);
  // For the secondary capturer, we do not have a backup plan anymore, so
  // FallbackDesktopCapturerWrapper won't check its return value any more. Its
  // results reach the input `callback` unchanged. If `outcome_recorder_` is
  // set, they pass through it first.
  if (outcome_recorder_) {
    outcome_recorder_->SetCallback(callback);
    secondary_capturer_->Start(outcome_recorder_.get());
  } else {
    secondary_capturer_->Start(callback);
  }
}

void FallbackDesktopCapturerWrapper::SetSharedMemoryFactory(
    std::unique_ptr<SharedMemoryFactory> shared_memory_factory) {
  shared_memory_factory_ = std::move(shared_memory_factory);
  if (shared_memory_factory_) {
    main_capturer_->SetSharedMemoryFactory(
        SharedMemoryFactoryProxy::Create(shared_memory_factory_.get()));
    secondary_capturer_->SetSharedMemoryFactory(
        SharedMemoryFactoryProxy::Create(shared_memory_factory_.get()));
  } else {
    main_capturer_->SetSharedMemoryFactory(
        std::unique_ptr<SharedMemoryFactory>());
    secondary_capturer_->SetSharedMemoryFactory(
        std::unique_ptr<SharedMemoryFactory>());
  }
}

void FallbackDesktopCapturerWrapper::CaptureFrame() {
  RTC_DCHECK(callback_);
  if (main_capturer_permanent_error_) {
    secondary_capturer_->CaptureFrame();
  } else {
    main_capturer_->CaptureFrame();
  }
}

void FallbackDesktopCapturerWrapper::SetExcludedWindow(WindowId window) {
  main_capturer_->SetExcludedWindow(window);
  secondary_capturer_->SetExcludedWindow(window);
}

bool FallbackDesktopCapturerWrapper::GetSourceList(SourceList* sources) {
  if (main_capturer_permanent_error_) {
    return secondary_capturer_->GetSourceList(sources);
  }
  return main_capturer_->GetSourceList(sources);
}

bool FallbackDesktopCapturerWrapper::SelectSource(SourceId id) {
  bool main_capturer_result = false;
  if (!main_capturer_permanent_error_) {
    main_capturer_result = main_capturer_->SelectSource(id);
    RTC_HISTOGRAM_BOOLEAN(
        "WebRTC.DesktopCapture.PrimaryCapturerSelectSourceError",
        main_capturer_result);
    if (!main_capturer_result) {
      main_capturer_permanent_error_ = true;
    }
  }

  const bool secondary_capturer_result = secondary_capturer_->SelectSource(id);
  if (outcome_recorder_) {
    outcome_recorder_->OnSelectSourceResult(main_capturer_result,
                                            secondary_capturer_result);
  }
  return secondary_capturer_result;
}

bool FallbackDesktopCapturerWrapper::FocusOnSelectedSource() {
  if (main_capturer_permanent_error_) {
    return secondary_capturer_->FocusOnSelectedSource();
  }
  return main_capturer_->FocusOnSelectedSource() ||
         secondary_capturer_->FocusOnSelectedSource();
}

bool FallbackDesktopCapturerWrapper::IsOccluded(const DesktopVector& pos) {
  // Returns true if either capturer returns true.
  if (main_capturer_permanent_error_) {
    return secondary_capturer_->IsOccluded(pos);
  }
  return main_capturer_->IsOccluded(pos) ||
         secondary_capturer_->IsOccluded(pos);
}

void FallbackDesktopCapturerWrapper::OnCaptureResult(
    Result result,
    std::unique_ptr<DesktopFrame> frame) {
  RTC_DCHECK(callback_);
  RTC_HISTOGRAM_BOOLEAN("WebRTC.DesktopCapture.PrimaryCapturerError",
                        result != Result::SUCCESS);
  RTC_HISTOGRAM_BOOLEAN("WebRTC.DesktopCapture.PrimaryCapturerPermanentError",
                        result == Result::ERROR_PERMANENT);
  if (outcome_recorder_) {
    outcome_recorder_->OnMainCaptureResult(result);
  }
  if (result == Result::SUCCESS) {
    callback_->OnCaptureResult(result, std::move(frame));
    return;
  }

  if (result == Result::ERROR_PERMANENT) {
    main_capturer_permanent_error_ = true;
  }
  secondary_capturer_->CaptureFrame();
}

}  // namespace webrtc
