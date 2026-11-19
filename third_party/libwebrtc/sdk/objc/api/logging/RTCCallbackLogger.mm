/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCCallbackLogger.h"

#import "helpers/NSString+StdString.h"

#include <memory>

#include "rtc_base/checks.h"
#include "rtc_base/log_sinks.h"
#include "rtc_base/logging.h"

namespace {

class CallbackLogSink final : public webrtc::LogSink {
 public:
  CallbackLogSink(RTCCallbackLoggerMessageHandler callbackHandler)
      : callback_handler_(callbackHandler) {}

  void OnLogMessage(const webrtc::LogLineRef& line) override {
    if (callback_handler_) {
      callback_handler_(
          [NSString stringForAbslStringView:line.DefaultLogLine()]);
    }
  }

 private:
  RTCCallbackLoggerMessageHandler callback_handler_;
};

class CallbackWithSeverityLogSink final : public webrtc::LogSink {
 public:
  CallbackWithSeverityLogSink(
      RTCCallbackLoggerMessageAndSeverityHandler callbackHandler)
      : callback_handler_(callbackHandler) {}

  void OnLogMessage(const webrtc::LogLineRef& line) override {
    if (callback_handler_) {
      RTCLoggingSeverity loggingSeverity =
          NativeSeverityToObjcSeverity(line.severity());
      callback_handler_(
          [NSString stringForAbslStringView:line.DefaultLogLine()],
          loggingSeverity);
    }
  }

 private:
  static RTCLoggingSeverity NativeSeverityToObjcSeverity(
      webrtc::LoggingSeverity severity) {
    switch (severity) {
      case webrtc::LS_VERBOSE:
        return RTCLoggingSeverityVerbose;
      case webrtc::LS_INFO:
        return RTCLoggingSeverityInfo;
      case webrtc::LS_WARNING:
        return RTCLoggingSeverityWarning;
      case webrtc::LS_ERROR:
        return RTCLoggingSeverityError;
      case webrtc::LS_NONE:
        return RTCLoggingSeverityNone;
    }
  }

  RTCCallbackLoggerMessageAndSeverityHandler callback_handler_;
};

}  // namespace

@implementation RTC_OBJC_TYPE (RTCCallbackLogger) {
  BOOL _hasStarted;
  std::unique_ptr<webrtc::LogSink> _logSink;
}

@synthesize severity = _severity;

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _severity = RTCLoggingSeverityInfo;
  }
  return self;
}

- (void)dealloc {
  [self stop];
}

- (void)start:(nullable RTCCallbackLoggerMessageHandler)handler {
  if (_hasStarted) {
    return;
  }

  _logSink.reset(new CallbackLogSink(handler));

  webrtc::LogMessage::AddLogToStream(_logSink.get(), [self rtcSeverity]);
  _hasStarted = YES;
}

- (void)startWithMessageAndSeverityHandler:
    (nullable RTCCallbackLoggerMessageAndSeverityHandler)handler {
  if (_hasStarted) {
    return;
  }

  _logSink.reset(new CallbackWithSeverityLogSink(handler));

  webrtc::LogMessage::AddLogToStream(_logSink.get(), [self rtcSeverity]);
  _hasStarted = YES;
}

- (void)stop {
  if (!_hasStarted) {
    return;
  }
  RTC_DCHECK(_logSink);
  webrtc::LogMessage::RemoveLogToStream(_logSink.get());
  _hasStarted = NO;
  _logSink.reset();
}

#pragma mark - Private

- (webrtc::LoggingSeverity)rtcSeverity {
  switch (_severity) {
    case RTCLoggingSeverityVerbose:
      return webrtc::LS_VERBOSE;
    case RTCLoggingSeverityInfo:
      return webrtc::LS_INFO;
    case RTCLoggingSeverityWarning:
      return webrtc::LS_WARNING;
    case RTCLoggingSeverityError:
      return webrtc::LS_ERROR;
    case RTCLoggingSeverityNone:
      return webrtc::LS_NONE;
  }
}

@end
