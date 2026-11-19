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

#if RTC_LOG_ENABLED()

#include <string.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/location.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/checks.h"
#include "rtc_base/platform_thread.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/clock.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::StartsWith;

#if defined(WEBRTC_WIN)
constexpr char kFakeFilePath[] = "some\\path\\myfile.cc";
#else
constexpr char kFakeFilePath[] = "some/path/myfile.cc";
#endif

// An array of unknown bound, i.e. of incomplete type until it is defined at the
// end of this file.
extern const char kUnboundedArray[];

struct ConvertibleToCharPointer {
  operator const char*() const { return "converted"; }
};

// Supports AbslStringify and also converts to a character pointer.
struct StringifiableAndConvertibleToCharPointer {
  operator const char*() const { return "converted"; }
};

template <typename Sink>
void AbslStringify(Sink& sink,
                   const StringifiableAndConvertibleToCharPointer& /*self*/) {
  sink.Append("stringified");
}

}  // namespace

class TestLogSink : public LogSink {
 public:
  absl::string_view data() const { return log_data_; }
  void Clear() { log_data_.clear(); }
  void Reserve(size_t size) { log_data_.reserve(size); }

 private:
  void OnLogMessage(const LogLineRef& line) override {
    log_data_.append(line.DefaultLogLine());
  }
  std::string log_data_;
};

// Overrides only the deprecated OnLogMessage(const std::string&), to test the
// default implementation of OnLogMessage(const LogLineRef&).
class DeprecatedTestLogSink : public LogSink {
 public:
  absl::string_view data() const { return log_data_; }

 private:
  void OnLogMessage(const std::string& message) override {
    log_data_.append(message);
  }
  std::string log_data_;
};

class LogMessageForTesting : public LogMessage {
 public:
  LogMessageForTesting(const char* file,
                       int line,
                       LoggingSeverity sev,
                       LogErrorContext err_ctx = ERRCTX_NONE,
                       int err = 0)
      : LogMessage(file, line, sev, err_ctx, err) {}

  const std::string& get_extra() const { return extra_; }
#if defined(WEBRTC_ANDROID)
  const char* get_tag() const { return log_line_.tag().data(); }
#endif

  // Returns the contents of the internal log stream.
  // Note that parts of the stream won't (as is) be available until *after* the
  // dtor of the parent class has run. So, as is, this only represents a
  // partially built stream.
  std::string GetPrintStream() {
    RTC_DCHECK(!is_finished_);
    is_finished_ = true;
    FinishPrintStream();
    return print_stream_.Release();
  }

 private:
  bool is_finished_ = false;
};

class LogTest : public ::testing::Test {
 protected:
  void SetUp() override { saved_config_ = ResetLoggingForTesting(); }
  void TearDown() override { ResetLoggingForTesting(std::move(saved_config_)); }

  // Initializes logging with `config` plus a sink of type `SinkType` and
  // returns the sink.
  template <typename SinkType = TestLogSink>
  SinkType& InitializeLoggingWithSink(LoggingConfig config = {}) {
    auto sink = std::make_unique<SinkType>();
    SinkType& sink_ref = *sink;
    config.AddSink(std::move(sink));
    RTC_CHECK(InitializeLogging(std::move(config)));
    return sink_ref;
  }

 private:
  LoggingConfig saved_config_;
};

// Test basic logging operation. We should get the INFO log but not the VERBOSE.
TEST_F(LogTest, SingleStream) {
  TestLogSink& sink = InitializeLoggingWithSink();

  RTC_LOG(LS_INFO) << "INFO";
  RTC_LOG(LS_VERBOSE) << "VERBOSE";
  EXPECT_THAT(sink.data(), HasSubstr("INFO"));
  EXPECT_THAT(sink.data(), Not(HasSubstr("VERBOSE")));

  int i = 1;
  long l = 2l;
  long long ll = 3ll;

  unsigned int u = 4u;
  unsigned long ul = 5ul;
  unsigned long long ull = 6ull;

  std::string s1 = "char*";
  std::string s2 = "std::string";
  std::string s3 = "absl::stringview";
  const char* null_string = nullptr;
  void* p = reinterpret_cast<void*>(0xabcd);

  // Log all supported types(except doubles/floats) as a sanity-check.
  RTC_LOG(LS_INFO) << "|" << i << "|" << l << "|" << ll << "|" << u << "|" << ul
                   << "|" << ull << "|" << s1.c_str() << "|" << s2 << "|"
                   << absl::string_view(s3) << "|" << p << "|" << null_string
                   << "|";

  // Signed integers
  EXPECT_THAT(sink.data(), HasSubstr("|1|"));
  EXPECT_THAT(sink.data(), HasSubstr("|2|"));
  EXPECT_THAT(sink.data(), HasSubstr("|3|"));

  // Unsigned integers
  EXPECT_THAT(sink.data(), HasSubstr("|4|"));
  EXPECT_THAT(sink.data(), HasSubstr("|5|"));
  EXPECT_THAT(sink.data(), HasSubstr("|6|"));

  // Strings
  EXPECT_THAT(sink.data(), HasSubstr("|char*|"));
  EXPECT_THAT(sink.data(), HasSubstr("|std::string|"));
  EXPECT_THAT(sink.data(), HasSubstr("|absl::stringview|"));

  // void*
  EXPECT_THAT(sink.data(), HasSubstr("|abcd|"));

  // null char*
  EXPECT_THAT(sink.data(), HasSubstr("|(null)|"));
}

TEST_F(LogTest, LogStdStringView) {
  TestLogSink& sink = InitializeLoggingWithSink();

  constexpr std::string_view kLongPath =
      "/a/very/long/path/string/that/exceeds/the/small/string/optimization/"
      "buffer/size/which/is/typically/around/fifteen/to/twenty/two/characters/"
      "and/this/is/definitely/longer/than/one/hundred/characters/to/trigger/"
      "any/potential/asan/errors";
  RTC_LOG(LS_INFO) << kLongPath;

  EXPECT_THAT(sink.data(), HasSubstr(kLongPath));
}

TEST_F(LogTest, LogLongDouble) {
  TestLogSink& sink = InitializeLoggingWithSink();

  long double ld = 123.456789L;
  RTC_LOG(LS_INFO) << ld;

  EXPECT_THAT(sink.data(), HasSubstr("123.456"));
}

// String literals provide their exact length as size hint. Other character
// pointers provide an estimate, since their length is only known at runtime.
static_assert(decltype(webrtc_logging_impl::MakeVal("abc"))::SizeHint() == 3);
static_assert(decltype(webrtc_logging_impl::MakeVal(
                  std::declval<const char*>()))::SizeHint() == 16);
// AbslStringify takes precedence over a conversion to a character pointer.
static_assert(std::is_same_v<decltype(webrtc_logging_impl::MakeVal(
                                 StringifiableAndConvertibleToCharPointer())),
                             webrtc_logging_impl::ToStringVal>);

TEST_F(LogTest, CharacterPointersAndArraysAreLoggedAsStrings) {
  TestLogSink& sink = InitializeLoggingWithSink();

  char buffer[32] = "buffer";
  char pointee[] = "pointer";
  char* pointer = pointee;
  const char* const_pointer = "const_pointer";
  RTC_LOG(LS_INFO) << "|" << "literal" << "|" << buffer << "|" << pointer << "|"
                   << const_pointer << "|" << kUnboundedArray << "|"
                   << ConvertibleToCharPointer() << "|"
                   << StringifiableAndConvertibleToCharPointer() << "|";

  EXPECT_THAT(sink.data(),
              HasSubstr("|literal|buffer|pointer|const_pointer|unbounded|"
                        "converted|stringified|"));
}

TEST_F(LogTest, MessageLongerThanSizeHintIsLoggedCompletely) {
  TestLogSink& sink = InitializeLoggingWithSink();

  // `long_pointer` adds more characters than its compile-time estimate, so the
  // message is longer than the reserved size.
  constexpr char kLiteral[] =
      "0123456789012345678901234567890123456789012345678901234567890123456789"
      "012345678901234567890123456789";
  const std::string long_string(1000, 's');
  const absl::string_view long_view = long_string;
  const char* long_pointer = long_string.c_str();
  RTC_LOG(LS_INFO) << kLiteral << kLiteral << kLiteral << long_string
                   << long_view << long_pointer;

  EXPECT_THAT(sink.data(),
              HasSubstr(std::string(kLiteral) + kLiteral + kLiteral +
                        long_string + long_string + long_string + "\n"));
}

TEST_F(LogTest, LogIfLogIfConditionIsTrue) {
  TestLogSink& sink = InitializeLoggingWithSink();

  RTC_LOG_IF(LS_INFO, true) << "Hello";
  EXPECT_THAT(sink.data(), HasSubstr("Hello"));
}

TEST_F(LogTest, LogIfDontLogIfConditionIsFalse) {
  TestLogSink& sink = InitializeLoggingWithSink();

  RTC_LOG_IF(LS_INFO, false) << "Hello";
  EXPECT_THAT(sink.data(), Not(HasSubstr("Hello")));
}

TEST_F(LogTest, LogIfFLogIfConditionIsTrue) {
  TestLogSink& sink = InitializeLoggingWithSink();

  RTC_LOG_IF_F(LS_INFO, true) << "Hello";
  EXPECT_THAT(sink.data(), HasSubstr(__FUNCTION__));
  EXPECT_THAT(sink.data(), HasSubstr("Hello"));
}

TEST_F(LogTest, LogIfFDontLogIfConditionIsFalse) {
  TestLogSink& sink = InitializeLoggingWithSink();

  RTC_LOG_IF_F(LS_INFO, false) << "Not";
  EXPECT_THAT(sink.data(), Not(HasSubstr(__FUNCTION__)));
  EXPECT_THAT(sink.data(), Not(HasSubstr("Not")));
}

// Test using multiple log streams. The INFO stream should get the INFO message,
// the VERBOSE stream should get the INFO and the VERBOSE.
// We should restore the correct global state at the end.
TEST_F(LogTest, MultipleStreams) {
  int sev = LogMessage::GetLogToStream(nullptr);

  TestLogSink stream1, stream2;
  LogMessage::AddLogToStream(&stream1, LS_INFO);
  LogMessage::AddLogToStream(&stream2, LS_VERBOSE);
  EXPECT_EQ(LS_INFO, LogMessage::GetLogToStream(&stream1));
  EXPECT_EQ(LS_VERBOSE, LogMessage::GetLogToStream(&stream2));

  RTC_LOG(LS_INFO) << "INFO";
  RTC_LOG(LS_VERBOSE) << "VERBOSE";

  EXPECT_THAT(stream1.data(), HasSubstr("INFO"));
  EXPECT_THAT(stream1.data(), Not(HasSubstr("VERBOSE")));
  EXPECT_THAT(stream2.data(), HasSubstr("INFO"));
  EXPECT_THAT(stream2.data(), HasSubstr("VERBOSE"));

  LogMessage::RemoveLogToStream(&stream2);
  LogMessage::RemoveLogToStream(&stream1);
  EXPECT_EQ(LS_NONE, LogMessage::GetLogToStream(&stream2));
  EXPECT_EQ(LS_NONE, LogMessage::GetLogToStream(&stream1));

  EXPECT_EQ(sev, LogMessage::GetLogToStream(nullptr));
}

class LogThread {
 public:
  void Start() {
    thread_ = PlatformThread::SpawnJoinable(
        [] { RTC_LOG(LS_VERBOSE) << "RTC_LOG"; }, "LogThread");
  }

 private:
  PlatformThread thread_;
};

// Ensure we don't crash when adding/removing streams while threads are going.
// We should restore the correct global state at the end.
TEST_F(LogTest, MultipleThreads) {
  int sev = LogMessage::GetLogToStream(nullptr);

  LogThread thread1, thread2, thread3;
  thread1.Start();
  thread2.Start();
  thread3.Start();

  TestLogSink stream1, stream2, stream3;
  for (int i = 0; i < 1000; ++i) {
    LogMessage::AddLogToStream(&stream1, LS_WARNING);
    LogMessage::AddLogToStream(&stream2, LS_INFO);
    LogMessage::AddLogToStream(&stream3, LS_VERBOSE);
    LogMessage::RemoveLogToStream(&stream1);
    LogMessage::RemoveLogToStream(&stream2);
    LogMessage::RemoveLogToStream(&stream3);
  }

  EXPECT_EQ(sev, LogMessage::GetLogToStream(nullptr));
}

TEST_F(LogTest, WallClockStartTime) {
  uint32_t time = LogMessage::WallClockStartTime();
  // Expect the time to be in a sensible range, e.g. > 2012-01-01.
  EXPECT_GT(time, 1325376000u);
}

TEST_F(LogTest, CheckExtraErrorField) {
  LogMessageForTesting log_msg(kFakeFilePath, 100, LS_WARNING, ERRCTX_ERRNO,
                               0xD);
  log_msg.stream() << "This gets added at dtor time";

  EXPECT_THAT(log_msg.get_extra(), StartsWith("[0x0000000D]"));
}

TEST_F(LogTest, LogErrnoAppendsExtraErrorField) {
  TestLogSink& sink = InitializeLoggingWithSink();

  RTC_LOG_ERRNO_EX(LS_INFO, EBADF) << "Message";

  EXPECT_THAT(sink.data(), HasSubstr("Message : [0x00000009]"));
}

TEST_F(LogTest, ReserveStreamAccountsForExtraErrorField) {
  LogMessageForTesting log_msg(kFakeFilePath, 100, LS_VERBOSE, ERRCTX_ERRNO,
                               EBADF);
  // The size of the message text, including the trailing newline.
  constexpr size_t kMessageSize = 100;
  log_msg.ReserveStream(kMessageSize);
  const char* buffer = log_msg.stream().str().data();
  log_msg.stream() << std::string(kMessageSize - 1, 'x');

  // Appends " : ", the extra error information and the newline.
  const std::string message = log_msg.GetPrintStream();
  EXPECT_THAT(message, SizeIs(kMessageSize + log_msg.get_extra().size() + 3));
  // The reserved buffer fits the complete message.
  EXPECT_EQ(message.data(), buffer);
}

TEST_F(LogTest, CheckFilePathParsed) {
  TestLogSink& sink = InitializeLoggingWithSink();
#if defined(WEBRTC_ANDROID)
  const char* tag = nullptr;
#endif
  {
    LogMessageForTesting log_msg(kFakeFilePath, 100, LS_INFO);
    log_msg.stream() << "<- Does this look right?";
#if defined(WEBRTC_ANDROID)
    tag = log_msg.get_tag();
#endif
  }

#if defined(WEBRTC_ANDROID)
  EXPECT_NE(nullptr, strstr(tag, "myfile.cc"));
  EXPECT_THAT(sink.data(), HasSubstr("100"));
#else
  EXPECT_THAT(sink.data(), HasSubstr("(myfile.cc:100)"));
#endif
}

// Tests that the default implementation of OnLogMessage(const LogLineRef&)
// passes the default log line on to the deprecated string overloads.
TEST_F(LogTest, DefaultOnLogMessageForwardsDefaultLogLine) {
  DeprecatedTestLogSink& sink =
      InitializeLoggingWithSink<DeprecatedTestLogSink>();
  {
    LogMessageForTesting log_msg(kFakeFilePath, 100, LS_INFO);
    log_msg.stream() << "Hello";
  }

#if defined(WEBRTC_ANDROID)
  // Prefixed with the tag, which is the file name by default.
  EXPECT_THAT(sink.data(), HasSubstr("myfile.cc: "));
  EXPECT_THAT(sink.data(), HasSubstr("(line 100): Hello"));
#else
  EXPECT_THAT(sink.data(), HasSubstr("(myfile.cc:100): Hello"));
#endif
}

// The debug output and all sinks get the same LogLineRef, so they share the
// line that DefaultLogLine() formats on first use.
TEST_F(LogTest, DefaultLogLineIsFormattedOnce) {
  class CachingCheckSink : public LogSink {
   public:
    void OnLogMessage(const LogLineRef& line) override {
      const std::string& first = line.DefaultLogLine();
      reused = &first == &line.DefaultLogLine();
    }
    bool reused = false;
  };
  CachingCheckSink& sink = InitializeLoggingWithSink<CachingCheckSink>();
  RTC_LOG(LS_INFO) << "Hello";
  EXPECT_TRUE(sink.reused);
}

// Some sinks store copies of LogLineRef, to inspect the log lines later.
TEST_F(LogTest, CopiedLogLineRefKeepsDefaultLogLine) {
  class CopyingSink : public LogSink {
   public:
    void OnLogMessage(const LogLineRef& line) override {
      lines.push_back(line);
    }
    std::vector<LogLineRef> lines;
  };
  CopyingSink& sink = InitializeLoggingWithSink<CopyingSink>();
  RTC_LOG(LS_INFO) << "Hello";
  ASSERT_THAT(sink.lines, SizeIs(1));
  EXPECT_THAT(sink.lines[0].DefaultLogLine(), HasSubstr("Hello"));
}

#if defined(WEBRTC_ANDROID)
TEST_F(LogTest, CheckTagAddedToStringInDefaultOnLogMessageAndroid) {
  DeprecatedTestLogSink& sink =
      InitializeLoggingWithSink<DeprecatedTestLogSink>();

  RTC_LOG_TAG(LS_INFO, "my_tag") << "INFO";
  EXPECT_THAT(sink.data(), HasSubstr("INFO"));
  EXPECT_THAT(sink.data(), HasSubstr("my_tag"));
}
#endif

// Test the time required to write 1000 80-character logs to a string.
TEST_F(LogTest, Perf) {
  // This test should probably be turned into a benchmark.
  Clock& clock = *Clock::GetRealTimeClock();
  LoggingConfig config;
  config.set_min_severity(LS_VERBOSE);
  config.set_debug_severity(LS_NONE);
  TestLogSink& sink = InitializeLoggingWithSink(std::move(config));

  const std::string message(80, 'X');
  {
    LogMessageForTesting sanity_check_msg(__FILE__, __LINE__, LS_VERBOSE);
  }

  // We now know how many bytes the logging framework will tag onto every msg.
  const size_t logging_overhead = sink.data().size();
  // Reset the stream to 0 size.
  sink.Clear();
  sink.Reserve(120000);
  static const int kRepetitions = 1000;

  Timestamp start = clock.CurrentTime();
  for (int i = 0; i < kRepetitions; ++i) {
    LogMessageForTesting(__FILE__, __LINE__, LS_VERBOSE).stream() << message;
  }
  Timestamp finish = clock.CurrentTime();

  EXPECT_EQ(sink.data().size(),
            (message.size() + logging_overhead) * kRepetitions);
  RTC_LOG(LS_INFO) << "Total log time: " << (finish - start)
                   << " total bytes logged: " << sink.data().size();
}

TEST_F(LogTest, EnumsAreSupported) {
  enum class TestEnum { kValue0 = 0, kValue1 = 1 };
  TestLogSink& sink = InitializeLoggingWithSink();
  RTC_LOG(LS_INFO) << "[" << TestEnum::kValue0 << "]";
  EXPECT_THAT(sink.data(), HasSubstr("[0]"));
  EXPECT_THAT(sink.data(), Not(HasSubstr("[1]")));
  RTC_LOG(LS_INFO) << "[" << TestEnum::kValue1 << "]";
  EXPECT_THAT(sink.data(), HasSubstr("[1]"));
}

TEST_F(LogTest, NoopSeverityDoesNotRunStringFormatting) {
  ASSERT_TRUE(LogMessage::IsNoop(LS_VERBOSE));
  bool was_called = false;
  auto cb = [&was_called]() {
    was_called = true;
    return "This could be an expensive callback.";
  };
  RTC_LOG(LS_VERBOSE) << "This should not be logged: " << cb();
  EXPECT_FALSE(was_called);
}

struct StructWithStringfy {
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const StructWithStringfy& /*self*/) {
    sink.Append("absl-stringify");
  }
};

TEST_F(LogTest, UseAbslStringForCustomTypes) {
  TestLogSink& sink = InitializeLoggingWithSink();
  StructWithStringfy t;

  RTC_LOG(LS_INFO) << t;

  EXPECT_THAT(sink.data(), HasSubstr("absl-stringify"));
}

enum class TestEnumStringify { kValue0 = 0, kValue1 = 1 };

template <typename Sink>
void AbslStringify(Sink& sink, TestEnumStringify value) {
  switch (value) {
    case TestEnumStringify::kValue0:
      sink.Append("kValue0");
      break;
    case TestEnumStringify::kValue1:
      sink.Append("kValue1");
      break;
  }
}

TEST_F(LogTest, EnumSupportsAbslStringify) {
  TestLogSink& sink = InitializeLoggingWithSink();
  RTC_LOG(LS_INFO) << "[" << TestEnumStringify::kValue0 << "]";
  EXPECT_THAT(sink.data(), HasSubstr("[kValue0]"));
  EXPECT_THAT(sink.data(), Not(HasSubstr("[kValue1]")));
  RTC_LOG(LS_INFO) << "[" << TestEnumStringify::kValue1 << "]";
  EXPECT_THAT(sink.data(), HasSubstr("[kValue1]"));
}

TEST_F(LogTest, ExplicitInitialization) {
  LoggingConfig config;
  config.set_min_severity(LS_WARNING);
  config.set_log_prefix("TEST_PREFIX: ");
  auto sink = std::make_unique<TestLogSink>();
  const TestLogSink* sink_ptr = sink.get();
  config.AddSink(std::move(sink));

  ASSERT_TRUE(InitializeLogging(std::move(config)));
  EXPECT_EQ(GetLoggingConfig().min_severity(), LS_WARNING);

  RTC_LOG(LS_WARNING) << "Test message";
  EXPECT_THAT(sink_ptr->data(), StartsWith("TEST_PREFIX: "));
  EXPECT_THAT(sink_ptr->data(), HasSubstr("Test message"));
}

TEST_F(LogTest, DoubleExplicitInitialization) {
  EXPECT_TRUE(InitializeLogging(LoggingConfig()));
  EXPECT_FALSE(InitializeLogging(LoggingConfig()));
}

TEST_F(LogTest, LogQueueNameFromThread) {
  LoggingConfig config;
  config.set_log_queue_name(true);
  TestLogSink& sink = InitializeLoggingWithSink(std::move(config));

  std::unique_ptr<Thread> thread = Thread::Create();
  thread->SetName("TName", nullptr);
  thread->Start();
  thread->BlockingCall([&]() { RTC_LOG(LS_INFO) << "Hello"; });

  EXPECT_THAT(sink.data(), HasSubstr("[TName]"));
}

class LogTestWithParam : public LogTest,
                         public ::testing::WithParamInterface<bool> {};

TEST_P(LogTestWithParam, LogQueueNameFromTaskQueueOverridingThread) {
  LoggingConfig config;
  config.set_log_thread(GetParam());
  config.set_log_queue_name(true);
  TestLogSink& sink = InitializeLoggingWithSink(std::move(config));

  std::unique_ptr<Thread> thread = Thread::Create();
  thread->SetName("TName", nullptr);
  thread->Start();

  class CustomTaskQueue : public TaskQueueBase {
   public:
    explicit CustomTaskQueue(absl::string_view name) : name_(name) {}
    void Delete() override {}
    void PostTaskImpl(absl::AnyInvocable<void() &&> task,
                      const PostTaskTraits& traits,
                      const Location& location) override {
      CurrentTaskQueueSetter set_current(this);
      std::move(task)();
    }
    void PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task,
                             TimeDelta delay,
                             const PostDelayedTaskTraits& traits,
                             const Location& location) override {
      RTC_DCHECK_NOTREACHED();
    }
    absl::string_view queue_name() const override { return name_; }

   private:
    std::string name_;
  };

  thread->BlockingCall([&]() {
    // 1. Verify thread name is printed first.
    RTC_LOG(LS_INFO) << "Prints the name of the thread.";
    EXPECT_THAT(sink.data(), HasSubstr("TName]"));
    sink.Clear();

    // 2. Set custom task queue.
    CustomTaskQueue custom_tq("QName");
    custom_tq.PostTask(
        [&]() { RTC_LOG(LS_INFO) << "Prints the name of the task queue."; });
    EXPECT_THAT(sink.data(), HasSubstr("QName]"));
    EXPECT_THAT(sink.data(), Not(HasSubstr("TName]")));
    sink.Clear();

    // 3. Verify fallback to thread name.
    RTC_LOG(LS_INFO) << "Prints the name of the thread again.";
    EXPECT_THAT(sink.data(), HasSubstr("TName]"));
  });
}

INSTANTIATE_TEST_SUITE_P(All, LogTestWithParam, ::testing::Bool());

namespace {
const char kUnboundedArray[] = "unbounded";
}  // namespace

}  // namespace webrtc
#endif  // RTC_LOG_ENABLED()
