/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/logging.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/strings/string_view.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/checks.h"
#include "rtc_base/platform_thread_types.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/string_utils.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"

#if RTC_LOG_ENABLED()

#if defined(WEBRTC_WIN)
#include <windows.h>
#undef ERROR  // wingdi.h
#endif

#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
#include <CoreServices/CoreServices.h>
#elif defined(WEBRTC_ANDROID)
#include <android/log.h>

// Android has a 1024 limit on log inputs. We use 60 chars as an
// approx for the header/tag portion.
// See android/system/core/liblog/logd_write.c
static const int kMaxLogLineSize = 1024 - 60;
#endif  // WEBRTC_MAC && !defined(WEBRTC_IOS) || WEBRTC_ANDROID

namespace webrtc {

bool LogMessage::aec_debug_ = false;
std::string LogMessage::aec_filename_base_;

void LogMessage::set_aec_debug(bool enable) {
  aec_debug_ = enable;
  webrtc::ApmDataDumper::SetActivated(aec_debug_);
}

std::string LogMessage::aec_debug_filename() {
  return aec_filename_base_;
}

void LogMessage::set_aec_debug_filename(const char* filename) {
  aec_filename_base_ = filename;
  webrtc::ApmDataDumper::SetOutputDirectory(aec_filename_base_);
}

namespace {

// By default, release builds don't log, debug builds at info level
#if !defined(NDEBUG)
constexpr LoggingSeverity kDefaultLoggingSeverity = LS_INFO;
#else
constexpr LoggingSeverity kDefaultLoggingSeverity = LS_NONE;
#endif

// Note: `g_min_sev` and `g_dbg_sev` can be changed while running.
constinit std::atomic<LoggingSeverity> g_min_sev = kDefaultLoggingSeverity;
constinit std::atomic<LoggingSeverity> g_dbg_sev = kDefaultLoggingSeverity;

// Return the filename portion of the string (that following the last slash).
const char* FilenameFromPath(const char* file) {
  const char* end1 = ::strrchr(file, '/');
  const char* end2 = ::strrchr(file, '\\');
  if (!end1 && !end2)
    return file;
  else
    return (end1 > end2) ? end1 + 1 : end2 + 1;
}

// Global lock for log subsystem, only needed to serialize access to streams_.
Mutex& GetLoggingLock() {
  static Mutex& mutex = *new Mutex();
  return mutex;
}

// Set by ResetLoggingForTesting() to let the next InitializeLogging() call
// replace the active configuration. Never set in production.
constinit bool g_reinitialize_for_testing RTC_GUARDED_BY(GetLoggingLock()) =
    false;

// Internal helper to manage config state.
// NOTE: The return value is non-const only while transitioning away from and
// deprecating legacy global methods that allow modifying the logging config
// after initialization. Once those methods are removed, this should return a
// const reference again to ensure lockless read-only access.
LoggingConfig& GetOrInitConfig(LoggingConfig* init_config = nullptr,
                               bool* config_applied = nullptr) {
  static absl::NoDestructor<LoggingConfig> config([&]() {
    if (config_applied != nullptr) {
      *config_applied = true;
    }
    if (init_config != nullptr) {
      return std::move(*init_config);
    }
    return LoggingConfig();  // Default configuration
  }());
  return *config;
}

void ApplyConfigToLogMessageLocked(const LoggingConfig& config)
    RTC_EXCLUSIVE_LOCKS_REQUIRED(GetLoggingLock()) {
  LogMessage::SetLogQueueNames(config.log_queue_name());
  LogMessage::LogTimestamps(config.log_timestamp());
  LogMessage::SetLogToStderr(config.log_to_stderr());
  g_dbg_sev.store(config.debug_severity(), std::memory_order_relaxed);
  LogMessage::UpdateMinLogSeverity();
}

}  // namespace

bool InitializeLogging(LoggingConfig config) {
  bool config_applied = false;
  LoggingConfig& active = GetOrInitConfig(&config, &config_applied);
  MutexLock lock(&GetLoggingLock());
  if (!config_applied && std::exchange(g_reinitialize_for_testing, false)) {
    active = std::move(config);
    config_applied = true;
  }
  if (config_applied) {
    ApplyConfigToLogMessageLocked(active);
  }
  return config_applied;
}

const LoggingConfig& GetLoggingConfig() {
  return GetOrInitConfig();
}

LoggingConfig ResetLoggingForTesting(
    std::optional<LoggingConfig> restore_config) {
  LoggingConfig& active = GetOrInitConfig();
  MutexLock lock(&GetLoggingLock());
  // Unless a configuration is restored, let the next InitializeLogging() call
  // replace the default configuration.
  g_reinitialize_for_testing = !restore_config.has_value();
  LoggingConfig previous = std::exchange(
      active, std::move(restore_config).value_or(LoggingConfig()));
  ApplyConfigToLogMessageLocked(active);
  return previous;
}

LogLineRef::LogLineRef() = default;

std::string LogLineRef::DefaultLogLine() const {
  absl::string_view prefix = GetLoggingConfig().log_prefix();
  StringBuilder log_output;
  // 48 characters is an estimate for the timestamp ("[000:000]"), the thread
  // and queue name ("[<thread_id>:<queue_name>] ") and the line number
  // ("(:<line>): ").
  log_output.Reserve(prefix.size() + filename_.size() + message_.size() + 48);
  log_output << prefix;
  if (timestamp_ != Timestamp::MinusInfinity()) {
    // TODO(kwiberg): Switch to absl::StrFormat, if binary size is ok.
    char timestamp[50];  // Maximum string length of an int64_t is 20.
    int len =
        snprintf(timestamp, sizeof(timestamp), "[%03" PRId64 ":%03" PRId64 "]",
                 timestamp_.ms() / 1000, timestamp_.ms() % 1000);
    RTC_DCHECK_LT(len, sizeof(timestamp));
    log_output << timestamp;
  }
  // Print thread information if requested. Depending on the settings and
  // available information, this will print:
  //   [<thread_id>]
  //   [<queue_or_thread_name>]
  //   [<thread_id>:<queue_or_thread_name>]
  if (thread_id_.has_value()) {
    log_output << "[" << *thread_id_;
    if (!queue_name_.empty()) {
      log_output << ":" << queue_name_.substr(0, 10);
    }
    log_output << "] ";
  } else if (!queue_name_.empty()) {
    log_output << "[" << queue_name_.substr(0, 10) << "] ";
  }
  if (!filename_.empty()) {
#if defined(WEBRTC_ANDROID)
    log_output << "(line " << line_ << "): ";
#else
    log_output << "(" << filename_ << ":" << line_ << "): ";
#endif
  }
  log_output << message_;
  return log_output.Release();
}

/////////////////////////////////////////////////////////////////////////////
// LogMessage
/////////////////////////////////////////////////////////////////////////////

bool LogMessage::log_to_stderr_ = false;

// The list of logging streams currently configured.
// Note: we explicitly do not clean this up, because of the uncertain ordering
// of destructors at program exit.  Let the person who sets the stream trigger
// cleanup by setting to null, or let it leak (safe at program exit).
constinit LogSink* LogMessage::streams_ RTC_GUARDED_BY(GetLoggingLock()) =
    nullptr;

// Boolean options default to false.
constinit bool LogMessage::log_timestamp_ = false;
constinit bool LogMessage::log_queue_name_ = false;

LogMessage::LogMessage(const char* file, int line, LoggingSeverity sev)
    : LogMessage(file, line, sev, ERRCTX_NONE, 0) {}

LogMessage::LogMessage(const char* file,
                       int line,
                       LoggingSeverity sev,
                       LogErrorContext err_ctx,
                       int err) {
  log_line_.set_severity(sev);
  const LoggingConfig& config = GetLoggingConfig();
  if (log_timestamp_) {
    int64_t log_start_time = LogStartTime();
    // Use SystemTimeMillis so that even if tests use fake clocks, the timestamp
    // in log messages represents the real system time.
    int64_t time = TimeDiff(SystemTimeMillis(), log_start_time);
    // Also ensure WallClockStartTime is initialized, so that it matches
    // LogStartTime.
    WallClockStartTime();
    log_line_.set_timestamp(Timestamp::Millis(time));
  }

  if (config.log_thread()) {
    log_line_.set_thread_id(CurrentThreadId());
  }

  if (log_queue_name_) {
    if (TaskQueueBase* tq = TaskQueueBase::Current(); tq != nullptr) {
      log_line_.set_queue_name(tq->queue_name());
    }
  }

  if (file != nullptr) {
    log_line_.set_filename(FilenameFromPath(file));
    log_line_.set_line(line);
#if defined(WEBRTC_ANDROID)
    log_line_.set_tag(log_line_.filename());
#endif
  }

  if (err_ctx != ERRCTX_NONE) {
    StringBuilder tmp;
    tmp.AppendFormat("[0x%08X]", err);
    switch (err_ctx) {
      case ERRCTX_ERRNO:
        tmp << " " << strerror(err);
        break;
#ifdef WEBRTC_WIN
      case ERRCTX_HRESULT: {
        char msgbuf[256];
        DWORD flags =
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        if (DWORD len = FormatMessageA(
                flags, nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                msgbuf, sizeof(msgbuf) / sizeof(msgbuf[0]), nullptr)) {
          while ((len > 0) &&
                 isspace(static_cast<unsigned char>(msgbuf[len - 1]))) {
            msgbuf[--len] = 0;
          }
          tmp << " " << msgbuf;
        }
        break;
      }
#endif  // WEBRTC_WIN
      default:
        break;
    }
    extra_ = tmp.Release();
  }
}

#if defined(WEBRTC_ANDROID)
LogMessage::LogMessage(const char* file,
                       int line,
                       LoggingSeverity sev,
                       const char* tag)
    : LogMessage(file, line, sev, ERRCTX_NONE, /*err=*/0) {
  log_line_.set_tag(tag);
  print_stream_ << tag << ": ";
}
#endif

LogMessage::~LogMessage() {
  FinishPrintStream();

  log_line_.set_message(print_stream_.Release());

  const LoggingConfig& config = GetLoggingConfig();

  if (log_line_.severity() >= config.debug_severity()) {
    OutputToDebug(log_line_);
  }

  if (log_line_.severity() >= config.min_severity()) {
    for (const auto& sink : config.sinks()) {
      sink->OnLogMessage(log_line_);
    }
  }

  MutexLock lock(&GetLoggingLock());
  for (LogSink* entry = streams_; entry != nullptr; entry = entry->next_) {
    if (log_line_.severity() >= entry->min_severity_) {
      entry->OnLogMessage(log_line_);
    }
  }
}

void LogMessage::AddTag([[maybe_unused]] const char* tag) {
#ifdef WEBRTC_ANDROID
  log_line_.set_tag(tag);
#endif
}

StringBuilder& LogMessage::stream() {
  return print_stream_;
}

void LogMessage::ReserveStream(size_t size) {
  // FinishPrintStream() appends " : " and `extra_`, if not empty.
  const size_t extra_size = extra_.empty() ? 0 : extra_.size() + 3;
  print_stream_.Reserve(print_stream_.size() + size + extra_size);
}

int LogMessage::GetMinLogSeverity() {
  return g_min_sev.load(std::memory_order_relaxed);
}

LoggingSeverity LogMessage::GetLogToDebug() {
  return g_dbg_sev.load(std::memory_order_relaxed);
}
int64_t LogMessage::LogStartTime() {
  static const int64_t g_start = SystemTimeMillis();
  return g_start;
}

uint32_t LogMessage::WallClockStartTime() {
  static const uint32_t g_start_wallclock = time(nullptr);
  return g_start_wallclock;
}

bool LogMessage::LogThreads(bool enabled) {
  LoggingConfig& config = GetOrInitConfig();
  bool prev = config.log_thread();
  config.set_log_thread(enabled);
  return prev;
}

bool LogMessage::SetLogQueueNames(bool enabled) {
  return std::exchange(log_queue_name_, enabled);
}

void LogMessage::LogTimestamps(bool on) {
  log_timestamp_ = on;
}

void LogMessage::LogToDebug(LoggingSeverity min_sev) {
  g_dbg_sev.store(min_sev, std::memory_order_relaxed);
  MutexLock lock(&GetLoggingLock());
  UpdateMinLogSeverity();
}

void LogMessage::SetLogToStderr(bool log_to_stderr) {
  log_to_stderr_ = log_to_stderr;
}

int LogMessage::GetLogToStream(LogSink* stream) {
  MutexLock lock(&GetLoggingLock());
  LoggingSeverity sev = LS_NONE;
  for (LogSink* entry = streams_; entry != nullptr; entry = entry->next_) {
    if (stream == nullptr || stream == entry) {
      sev = std::min(sev, entry->min_severity_);
    }
  }
  return sev;
}

void LogMessage::AddLogToStream(LogSink* stream, LoggingSeverity min_sev) {
  MutexLock lock(&GetLoggingLock());
  stream->min_severity_ = min_sev;
  stream->next_ = streams_;
  streams_ = stream;
  UpdateMinLogSeverity();
}

void LogMessage::RemoveLogToStream(LogSink* stream) {
  MutexLock lock(&GetLoggingLock());
  for (LogSink** entry = &streams_; *entry != nullptr;
       entry = &(*entry)->next_) {
    if (*entry == stream) {
      *entry = (*entry)->next_;
      break;
    }
  }
  UpdateMinLogSeverity();
}

void LogMessage::ConfigureLogging(absl::string_view params) {
  LoggingSeverity current_level = LS_VERBOSE;
  LoggingSeverity debug_level = GetLogToDebug();

  std::vector<std::string> tokens;
  tokenize(params, ' ', &tokens);

  for (const std::string& token : tokens) {
    if (token.empty())
      continue;

    // Logging features
    if (token == "tstamp") {
      LogTimestamps();
    } else if (token == "thread") {
      GetOrInitConfig().set_log_thread(true);

      // Logging levels
    } else if (token == "verbose") {
      current_level = LS_VERBOSE;
    } else if (token == "info") {
      current_level = LS_INFO;
    } else if (token == "warning") {
      current_level = LS_WARNING;
    } else if (token == "error") {
      current_level = LS_ERROR;
    } else if (token == "none") {
      current_level = LS_NONE;

      // Logging targets
    } else if (token == "debug") {
      debug_level = current_level;
    }
  }

#if defined(WEBRTC_WIN) && !defined(WINUWP)
  if ((LS_NONE != debug_level) && !::IsDebuggerPresent()) {
    // First, attempt to attach to our parent's console... so if you invoke
    // from the command line, we'll see the output there.  Otherwise, create
    // our own console window.
    // Note: These methods fail if a console already exists, which is fine.
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
      ::AllocConsole();
  }
#endif  // defined(WEBRTC_WIN) && !defined(WINUWP)

  LogToDebug(debug_level);
}

void LogMessage::UpdateMinLogSeverity()
    RTC_EXCLUSIVE_LOCKS_REQUIRED(GetLoggingLock()) {
  LoggingSeverity min_sev = g_dbg_sev.load(std::memory_order_relaxed);
  const LoggingConfig& config = GetLoggingConfig();
  min_sev = std::min(min_sev, config.min_severity());
  for (LogSink* entry = streams_; entry != nullptr; entry = entry->next_) {
    min_sev = std::min(min_sev, entry->min_severity_);
  }
  g_min_sev.store(min_sev, std::memory_order_relaxed);
}

void LogMessage::OutputToDebug(const LogLineRef& log_line) {
  std::string msg_str = log_line.DefaultLogLine();
  bool log_to_stderr = log_to_stderr_;
#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS) && defined(NDEBUG)
  // On the Mac, all stderr output goes to the Console log and causes clutter.
  // So in opt builds, don't log to stderr unless the user specifically sets
  // a preference to do so.
  CFStringRef domain = CFBundleGetIdentifier(CFBundleGetMainBundle());
  if (domain != nullptr) {
    Boolean exists_and_is_valid;
    Boolean should_log = CFPreferencesGetAppBooleanValue(
        CFSTR("logToStdErr"), domain, &exists_and_is_valid);
    // If the key doesn't exist or is invalid or is false, we will not log to
    // stderr.
    log_to_stderr = exists_and_is_valid && should_log;
  }
#endif  // defined(WEBRTC_MAC) && !defined(WEBRTC_IOS) && defined(NDEBUG)

#if defined(WEBRTC_WIN)
  // Always log to the debugger.
  // Perhaps stderr should be controlled by a preference, as on Mac?
#if RTC_DLOG_IS_ON
  OutputDebugStringA(msg_str.c_str());
#endif
  if (log_to_stderr) {
    // This handles dynamically allocated consoles, too.
    if (HANDLE error_handle = ::GetStdHandle(STD_ERROR_HANDLE)) {
      log_to_stderr = false;
      DWORD written = 0;
      ::WriteFile(error_handle, msg_str.c_str(),
                  static_cast<DWORD>(msg_str.size()), &written, 0);
    }
  }
#endif  // WEBRTC_WIN

#if defined(WEBRTC_ANDROID)
  // Android's logging facility uses severity to log messages but we
  // need to map libjingle's severity levels to Android ones first.
  // Also write to stderr which maybe available to executable started
  // from the shell.
  int prio;
  switch (log_line.severity()) {
    case LS_VERBOSE:
      prio = ANDROID_LOG_VERBOSE;
      break;
    case LS_INFO:
      prio = ANDROID_LOG_INFO;
      break;
    case LS_WARNING:
      prio = ANDROID_LOG_WARN;
      break;
    case LS_ERROR:
      prio = ANDROID_LOG_ERROR;
      break;
    default:
      prio = ANDROID_LOG_UNKNOWN;
  }

  int size = msg_str.size();
  int current_line = 0;
  int idx = 0;
  const int max_lines = size / kMaxLogLineSize + 1;
  if (max_lines == 1) {
    __android_log_print(prio, log_line.tag().data(), "%.*s", size,
                        msg_str.c_str());
  } else {
    while (size > 0) {
      const int len = std::min(size, kMaxLogLineSize);
      // Use the size of the string in the format (msg may have \0 in the
      // middle).
      __android_log_print(prio, log_line.tag().data(), "[%d/%d] %.*s",
                          current_line + 1, max_lines, len,
                          msg_str.c_str() + idx);
      idx += len;
      size -= len;
      ++current_line;
    }
  }
#endif  // WEBRTC_ANDROID
  if (log_to_stderr) {
    fprintf(stderr, "%s", msg_str.c_str());
    fflush(stderr);
  }
}

bool LogMessage::IsNoop(LoggingSeverity severity) {
  return severity < g_min_sev.load(std::memory_order_relaxed);
}

void LogMessage::FinishPrintStream() {
  if (!extra_.empty())
    print_stream_ << " : " << extra_;
  print_stream_ << "\n";
}

namespace webrtc_logging_impl {
namespace {

// Returns the total size of the std::string and string view arguments in
// `args`, whose types are listed in `fmt`. The sizes of the other arguments
// are estimated at compile time, see LogStreamer<>::Call().
size_t StringArgsSize(const LogArgType* fmt, va_list args) {
  size_t size = 0;
  for (; *fmt != LogArgType::kEnd; ++fmt) {
    switch (*fmt) {
      case LogArgType::kInt:
        va_arg(args, int);
        break;
      case LogArgType::kLong:
        va_arg(args, long);
        break;
      case LogArgType::kLongLong:
        va_arg(args, long long);
        break;
      case LogArgType::kUInt:
        va_arg(args, unsigned);
        break;
      case LogArgType::kULong:
        va_arg(args, unsigned long);
        break;
      case LogArgType::kULongLong:
        va_arg(args, unsigned long long);
        break;
      case LogArgType::kDouble:
        va_arg(args, double);
        break;
      case LogArgType::kLongDouble:
        va_arg(args, long double);
        break;
      case LogArgType::kCharP:
        va_arg(args, const char*);
        break;
      case LogArgType::kVoidP:
        va_arg(args, const void*);
        break;
      case LogArgType::kStdString:
        size += va_arg(args, const std::string*)->size();
        break;
      case LogArgType::kStringView:
        size += va_arg(args, const absl::string_view*)->size();
        break;
      case LogArgType::kStdStringView:
        size += va_arg(args, const std::string_view*)->size();
        break;
      case LogArgType::kEnd:
      case LogArgType::kLogMetadata:
      case LogArgType::kLogMetadataErr:
#ifdef WEBRTC_ANDROID
      case LogArgType::kLogMetadataTag:
#else
      case LogArgType::kReserved:
#endif
        // Unexpected types are reported when the message is formatted.
        return size;
    }
  }
  return size;
}

}  // namespace

void Log(size_t size_hint, const LogArgType* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  LogMetadataErr meta;
  const char* tag = nullptr;
  switch (*fmt) {
    case LogArgType::kLogMetadata: {
      meta = {
          .meta = va_arg(args, LogMetadata), .err_ctx = ERRCTX_NONE, .err = 0};
      break;
    }
    case LogArgType::kLogMetadataErr: {
      meta = va_arg(args, LogMetadataErr);
      break;
    }
#ifdef WEBRTC_ANDROID
    case LogArgType::kLogMetadataTag: {
      const LogMetadataTag tag_meta = va_arg(args, LogMetadataTag);
      meta = {{nullptr, 0, tag_meta.severity}, ERRCTX_NONE, 0};
      tag = tag_meta.tag;
      break;
    }
#else
    case LogArgType::kReserved:
#endif
    case LogArgType::kEnd:
    case LogArgType::kInt:
    case LogArgType::kLong:
    case LogArgType::kLongLong:
    case LogArgType::kUInt:
    case LogArgType::kULong:
    case LogArgType::kULongLong:
    case LogArgType::kDouble:
    case LogArgType::kLongDouble:
    case LogArgType::kCharP:
    case LogArgType::kStdString:
    case LogArgType::kStringView:
    case LogArgType::kVoidP:
    case LogArgType::kStdStringView: {
      RTC_DCHECK_NOTREACHED();
      va_end(args);
      return;
    }
  }

  LogMessage log_message(meta.meta.File(), meta.meta.Line(),
                         meta.meta.Severity(), meta.err_ctx, meta.err);
  if (tag) {
    log_message.AddTag(tag);
  }

  // Add the sizes of the string arguments, which are only known at runtime, so
  // that the message can typically be formatted with a single allocation.
  va_list size_args;
  va_copy(size_args, args);
  const size_t reserve_size = size_hint + StringArgsSize(fmt + 1, size_args);
  va_end(size_args);
  log_message.ReserveStream(reserve_size);

  for (++fmt; *fmt != LogArgType::kEnd; ++fmt) {
    switch (*fmt) {
      case LogArgType::kInt:
        log_message.stream() << va_arg(args, int);
        break;
      case LogArgType::kLong:
        log_message.stream() << va_arg(args, long);
        break;
      case LogArgType::kLongLong:
        log_message.stream() << va_arg(args, long long);
        break;
      case LogArgType::kUInt:
        log_message.stream() << va_arg(args, unsigned);
        break;
      case LogArgType::kULong:
        log_message.stream() << va_arg(args, unsigned long);
        break;
      case LogArgType::kULongLong:
        log_message.stream() << va_arg(args, unsigned long long);
        break;
      case LogArgType::kDouble:
        log_message.stream() << va_arg(args, double);
        break;
      case LogArgType::kLongDouble:
        log_message.stream().AppendFormat("%Lf", va_arg(args, long double));
        break;
      case LogArgType::kCharP: {
        const char* s = va_arg(args, const char*);
        log_message.stream() << (s ? s : "(null)");
        break;
      }
      case LogArgType::kStdString:
        log_message.stream() << *va_arg(args, const std::string*);
        break;
      case LogArgType::kStringView:
        log_message.stream() << *va_arg(args, const absl::string_view*);
        break;
      case LogArgType::kStdStringView:
        log_message.stream() << *va_arg(args, const std::string_view*);
        break;
      case LogArgType::kVoidP:
        log_message.stream()
            << ToHex(reinterpret_cast<uintptr_t>(va_arg(args, const void*)));
        break;
      case LogArgType::kEnd:
      case LogArgType::kLogMetadata:
      case LogArgType::kLogMetadataErr:
#ifdef WEBRTC_ANDROID
      case LogArgType::kLogMetadataTag:
#else
      case LogArgType::kReserved:
#endif
      {
        RTC_DCHECK_NOTREACHED();
        va_end(args);
        return;
      }
    }
  }

  va_end(args);
}

}  // namespace webrtc_logging_impl
}  // namespace webrtc
#else   // RTC_LOG_ENABLED()
namespace webrtc {
bool InitializeLogging(LoggingConfig /* config */) {
  return false;
}

const LoggingConfig& GetLoggingConfig() {
  static const absl::NoDestructor<LoggingConfig> config;
  return *config;
}

LoggingConfig ResetLoggingForTesting(
    std::optional<LoggingConfig> /* restore_config */) {
  return LoggingConfig();
}
}  // namespace webrtc
#endif  // RTC_LOG_ENABLED()

namespace webrtc {
LoggingConfig::LoggingConfig() = default;
LoggingConfig::LoggingConfig(LoggingConfig&&) = default;
LoggingConfig& LoggingConfig::operator=(LoggingConfig&&) = default;
LoggingConfig::~LoggingConfig() = default;

// Default implementation, override is recomended.
void LogSink::OnLogMessage(const LogLineRef& log_line) {
#if defined(WEBRTC_ANDROID)
  OnLogMessage(log_line.DefaultLogLine(), log_line.severity(),
               log_line.tag().data());
#else
  OnLogMessage(log_line.DefaultLogLine(), log_line.severity());
#endif
}

// Inefficient default implementation, override is recommended.
void LogSink::OnLogMessage(const std::string& msg,
                           LoggingSeverity severity,
                           const char* tag) {
  OnLogMessage(tag + (": " + msg), severity);
}

void LogSink::OnLogMessage(const std::string& msg,
                           LoggingSeverity /* severity */) {
  OnLogMessage(msg);
}

// Inefficient default implementation, override is recommended.
void LogSink::OnLogMessage(absl::string_view msg,
                           LoggingSeverity severity,
                           const char* tag) {
  OnLogMessage(tag + (": " + std::string(msg)), severity);
}

void LogSink::OnLogMessage(absl::string_view msg,
                           LoggingSeverity /* severity */) {
  OnLogMessage(msg);
}

void LogSink::OnLogMessage(absl::string_view msg) {
  OnLogMessage(std::string(msg));
}

}  // namespace webrtc
