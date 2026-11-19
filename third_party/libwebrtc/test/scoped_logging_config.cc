/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/scoped_logging_config.h"

#include <utility>

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

ScopedLoggingConfig::ScopedLoggingConfig(LoggingConfig config)
    : saved_config_(ResetLoggingForTesting()) {
  // ResetLoggingForTesting() lets the next InitializeLogging() call succeed.
  RTC_CHECK(InitializeLogging(std::move(config)));
}

ScopedLoggingConfig::~ScopedLoggingConfig() {
  ResetLoggingForTesting(std::move(saved_config_));
}

}  // namespace webrtc
