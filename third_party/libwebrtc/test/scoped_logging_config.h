/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_SCOPED_LOGGING_CONFIG_H_
#define TEST_SCOPED_LOGGING_CONFIG_H_

#include "rtc_base/logging.h"

namespace webrtc {

// Applies `config` as the active logging configuration for the lifetime of
// this object and restores the previous configuration when destroyed.
// No other thread may log while the object is constructed or destroyed, since
// that replaces the configuration that log messages read.
class ScopedLoggingConfig {
 public:
  explicit ScopedLoggingConfig(LoggingConfig config);
  ScopedLoggingConfig(const ScopedLoggingConfig&) = delete;
  ScopedLoggingConfig& operator=(const ScopedLoggingConfig&) = delete;
  ~ScopedLoggingConfig();

 private:
  LoggingConfig saved_config_;
};

}  // namespace webrtc

#endif  // TEST_SCOPED_LOGGING_CONFIG_H_
