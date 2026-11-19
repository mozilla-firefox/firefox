<!-- go/cmark -->

<!--* freshness: {owner: 'tommi' reviewed: '2026-10-27'} *-->

# WebRTC Logging Initialization

## Overview

This document explains how to initialize the WebRTC logging system, when it must
be done, and the consequences of late initialization.

## How to Initialize

To initialize logging with custom settings, call `webrtc::InitializeLogging` and
pass a `webrtc::LoggingConfig` object.

**Initialization can happen only once.**

Example:

```cpp
#include "rtc_base/logging.h"

int main() {
  webrtc::LoggingConfig config;
  config.set_min_severity(webrtc::LS_INFO);
  config.set_log_thread(true);
  config.set_log_timestamp(true);

  // Add custom sinks if needed
  // config.AddSink(std::make_unique<MyLogSink>());

  if (!webrtc::InitializeLogging(std::move(config))) {
    // Handle initialization failure (e.g., called too late)
  }

  // Proceed with WebRTC usage
}
```

`webrtc::GetLoggingConfig()` returns the active configuration.

## Configuration Options

| Setter                 | Default   | Description                             |
| ---------------------- | --------- | --------------------------------------- |
| `set_min_severity()`   | `LS_INFO` | Minimum severity for `AddSink()` sinks. |
| `set_debug_severity()` | See below | Minimum severity for the debug output.  |
| `set_log_to_stderr()`  | `true`    | Writes the debug output to stderr.      |
| `set_log_thread()`     | `false`   | Adds the thread ID to log lines.        |
| `set_log_queue_name()` | `false`   | Adds the task queue name to log lines.  |
| `set_log_timestamp()`  | `false`   | Adds the elapsed time to log lines.     |
| `set_log_prefix()`     | Empty     | Text that starts each log line.         |

The default debug severity is `LS_INFO` in debug builds and `LS_NONE` in release
builds, so release builds don't write to the debug output unless configured to.
Depending on the platform, the debug output also goes to logcat (Android) or to
the debugger (Windows debug builds).

The legacy `LogMessage::LogToDebug()` changes the minimum severity for the debug
output at runtime, also after initialization. `GetLoggingConfig()` keeps
returning the initial value.

## Log Sinks

A `webrtc::LogSink` receives log messages, for example to write them to a file.
Sinks added with `LoggingConfig::AddSink()` receive each message whose severity
is at least `min_severity()`. A sink overrides
`OnLogMessage(const LogLineRef&)`; the overloads that take a string are
deprecated.

```cpp
class MyLogSink : public webrtc::LogSink {
 public:
  void OnLogMessage(const webrtc::LogLineRef& line) override {
    // `line.DefaultLogLine()` returns the message in the default format.
  }
};
```

The logging system owns the sinks for the remainder of the process. Sinks are
called on the thread that logs, without synchronization, so they must be
thread-safe.

## When Initialization Must Be Done

Initialization **must** be performed before any other WebRTC API calls and
before any logging occurs via `RTC_LOG` macros.

## Implicit Initialization

If not initialized explicitly, logging initialization will happen **implicitly**
upon the first logging call (e.g., `RTC_LOG`) or any function that accesses the
logging configuration, such as `GetLoggingConfig()` or the legacy
`LogMessage::AddLogToStream()` and `LogMessage::LogToDebug()`. The defaults used
for implicit initialization are defined in the `LoggingConfig` class, see
[Configuration Options](#configuration-options).

## Behavior When It Is Too Late

If `InitializeLogging` is called after the logging system has already been
initialized (implicitly or explicitly):

1. The function will return `false`.
2. The provided `LoggingConfig` will be ignored.
3. The logging system will continue to operate with the configuration
   established by the first initialization.

To avoid this, ensure `InitializeLogging` is called as early as possible in the
application's startup sequence.

## Tests

WebRTC test binaries initialize logging in
[`test/test_main_lib.cc`](/test/test_main_lib.cc), based on the `--log_level`,
`--logs` and `--verbose` flags, so `InitializeLogging` returns `false` in tests.
A test that needs a different configuration can call
`webrtc::ResetLoggingForTesting()`, after which the next `InitializeLogging`
call succeeds, and restore the previous configuration when done:

```cpp
class MyLoggingTest : public ::testing::Test {
 protected:
  void SetUp() override { saved_config_ = webrtc::ResetLoggingForTesting(); }
  void TearDown() override {
    webrtc::ResetLoggingForTesting(std::move(saved_config_));
  }

 private:
  webrtc::LoggingConfig saved_config_;
};
```
