/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_DESKTOP_CAPTURE_FALLBACK_DESKTOP_CAPTURER_WRAPPER_H_
#define MODULES_DESKTOP_CAPTURE_FALLBACK_DESKTOP_CAPTURER_WRAPPER_H_

#include <memory>

#include "absl/strings/string_view.h"
#include "modules/desktop_capture/desktop_capture_types.h"
#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/desktop_frame.h"
#include "modules/desktop_capture/desktop_geometry.h"
#include "modules/desktop_capture/shared_memory.h"

namespace webrtc {

// A DesktopCapturer wrapper owns two DesktopCapturer implementations. If the
// main DesktopCapturer fails, it uses the secondary one instead. Two capturers
// are expected to return same SourceList, and the meaning of each SourceId is
// identical, otherwise FallbackDesktopCapturerWrapper may return frames from
// different sources. Using asynchronized DesktopCapturer implementations with
// SharedMemoryFactory is not supported, and may result crash or assertion
// failure.
class FallbackDesktopCapturerWrapper final : public DesktopCapturer,
                                             public DesktopCapturer::Callback {
 public:
  // How a capture session started, recorded at most once to `start_histogram`:
  // - `Accepted` / `Rejected`: capturer's SelectSource() result.
  // - `StartError`: main returned ERROR_PERMANENT before its first frame.
  // - `Worked`: capturer delivered a frame (Result::SUCCESS).
  // - `Failed`: secondary returned ERROR_PERMANENT, or returned
  //   ERROR_TEMPORARY on every CaptureFrame() call before destruction.
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class StartOutcome {
    kMainWorked = 0,
    kMainAcceptedSecondaryRejected = 1,
    kMainRejectedSecondaryRejected = 2,
    kMainRejectedSecondaryWorked = 3,
    kMainRejectedSecondaryFailed = 4,
    kMainStartErrorSecondaryWorked = 5,
    kMainStartErrorSecondaryFailed = 6,
    kMaxValue = kMainStartErrorSecondaryFailed,
  };

  // If `start_histogram` is non-empty, records the first StartOutcome to it
  // (assuming SelectSource() is called at least once before CaptureFrame()):
  // in SelectSource() if the secondary capturer rejects the source, or
  // otherwise after CaptureFrame() starts.
  FallbackDesktopCapturerWrapper(
      std::unique_ptr<DesktopCapturer> main_capturer,
      std::unique_ptr<DesktopCapturer> secondary_capturer,
      absl::string_view start_histogram = "");
  ~FallbackDesktopCapturerWrapper() override;

  // DesktopCapturer interface.
  void Start(DesktopCapturer::Callback* callback) override;
  void SetSharedMemoryFactory(
      std::unique_ptr<SharedMemoryFactory> shared_memory_factory) override;
  void CaptureFrame() override;
  void SetExcludedWindow(WindowId window) override;
  bool GetSourceList(SourceList* sources) override;
  bool SelectSource(SourceId id) override;
  bool FocusOnSelectedSource() override;
  bool IsOccluded(const DesktopVector& pos) override;

 private:
  class OutcomeRecorder;

  // DesktopCapturer::Callback interface.
  void OnCaptureResult(Result result,
                       std::unique_ptr<DesktopFrame> frame) override;

  // Declared before `secondary_capturer_`, which holds a raw pointer to it.
  const std::unique_ptr<OutcomeRecorder> outcome_recorder_;
  const std::unique_ptr<DesktopCapturer> main_capturer_;
  const std::unique_ptr<DesktopCapturer> secondary_capturer_;
  std::unique_ptr<SharedMemoryFactory> shared_memory_factory_;
  bool main_capturer_permanent_error_ = false;
  DesktopCapturer::Callback* callback_ = nullptr;
};

}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_FALLBACK_DESKTOP_CAPTURER_WRAPPER_H_
