/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/test_main_lib.h"

#include <stdlib.h>

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/test/metrics/chrome_perf_dashboard_metrics_exporter.h"
#include "api/test/metrics/global_metrics_logger_and_exporter.h"
#include "api/test/metrics/metric.h"
#include "api/test/metrics/metrics_exporter.h"
#include "api/test/metrics/metrics_set_proto_file_exporter.h"
#include "api/test/metrics/stdout_metrics_exporter.h"
#include "rtc_base/checks.h"
#include "rtc_base/event_tracer.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/test_flags.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/resources_dir_flag.h"

#if defined(RTC_USE_PERFETTO)
#include "rtc_base/event_tracer.h"
#include "third_party/perfetto/include/perfetto/tracing/backend_type.h"  // nogncheck
#include "third_party/perfetto/include/perfetto/tracing/tracing.h"  // nogncheck
#include "third_party/perfetto/protos/perfetto/config/trace_config.gen.h"  // nogncheck
#endif

#if defined(WEBRTC_WIN)
#include "rtc_base/win32_socket_init.h"
#endif

#if defined(WEBRTC_IOS)
#include "test/ios/test_support.h"

ABSL_FLAG(std::string,
          NSTreatUnknownArgumentsAsOpen,
          "",
          "Intentionally ignored flag intended for iOS test runner.");
ABSL_FLAG(std::string,
          ApplePersistenceIgnoreState,
          "",
          "Intentionally ignored flag intended for iOS test runner.");
ABSL_FLAG(bool,
          enable_run_ios_unittests_with_xctest,
          false,
          "Intentionally ignored flag intended for iOS test runner.");
ABSL_FLAG(bool,
          write_compiled_tests_json_to_writable_path,
          false,
          "Intentionally ignored flag intended for iOS test runner.");

// This is the cousin of isolated_script_test_perf_output, but we can't dictate
// where to write on iOS so the semantics of this flag are a bit different.
ABSL_FLAG(
    bool,
    write_perf_output_on_ios,
    false,
    "Store the perf results in Documents/perftest_result.pb in the format "
    "described by histogram.proto in "
    "https://chromium.googlesource.com/catapult/.");

#elif defined(WEBRTC_FUCHSIA)
ABSL_FLAG(std::string, use_vulkan, "", "Intentionally ignored flag.");
#else
// TODO(bugs.webrtc.org/8115): Remove workaround when fixed.
ABSL_FLAG(bool, no_sandbox, false, "Intentionally ignored flag.");
ABSL_FLAG(bool, test_launcher_bot_mode, false, "Intentionally ignored flag.");
#endif

ABSL_FLAG(std::string,
          webrtc_test_metrics_output_path,
          "",
          "Path where the test perf metrics should be stored using "
          "api/test/metrics/metric.proto proto format. File will contain "
          "MetricsSet as a root proto. On iOS, this MUST be a file name "
          "and the file will be stored under NSDocumentDirectory.");

ABSL_FLAG(std::string,
          isolated_script_test_output,
          "",
          "Path to output an empty JSON file which Chromium infra requires.");

ABSL_FLAG(bool, logs, true, "print logs to stderr");
ABSL_FLAG(bool, verbose, false, "verbose logs to stderr");

namespace webrtc {

bool AbslParseFlag(absl::string_view text,
                   LoggingSeverity* severity,
                   std::string* error) {
  RTC_DCHECK(severity);
  RTC_DCHECK(error);
  if (absl::EqualsIgnoreCase(text, "verbose")) {
    *severity = LS_VERBOSE;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "info")) {
    *severity = LS_INFO;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "warning")) {
    *severity = LS_WARNING;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "error")) {
    *severity = LS_ERROR;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "none")) {
    *severity = LS_NONE;
    return true;
  }
  *error = absl::StrCat("unknown log level: ", text);
  return false;
}

std::string AbslUnparseFlag(LoggingSeverity severity) {
  switch (severity) {
    case LS_VERBOSE:
      return "verbose";
    case LS_INFO:
      return "info";
    case LS_WARNING:
      return "warning";
    case LS_ERROR:
      return "error";
    case LS_NONE:
      return "none";
  }
  return "unknown";
}

}  // namespace webrtc

ABSL_FLAG(webrtc::LoggingSeverity,
          log_level,
          webrtc::LS_WARNING,
          "Minimum log level: verbose, info, warning, error, none");

ABSL_FLAG(std::string,
          trace_event,
          "",
          "Path to collect trace events. If not set, events aren't captured.");

ABSL_FLAG(std::string,
          test_launcher_shard_index,
          "",
          "Index of the test shard to run, from 0 to "
          "the value specified with --test_launcher_total_shards.");

ABSL_FLAG(std::string,
          test_launcher_total_shards,
          "",
          "Total number of shards.");

namespace webrtc {

namespace {

constexpr char kPlotAllMetrics[] = "all";

class TestMainImpl : public TestMain {
 public:
  int Init(int* argc, char* argv[]) override { return Init(); }

  int Init() override {
    // Make sure we always pull in the --resources_dir flag, even if the test
    // binary doesn't link with fileutils (downstream expects all test mains to
    // have this flag).
    (void)absl::GetFlag(FLAGS_resources_dir);

    LoggingConfig config;
    const bool verbose = absl::GetFlag(FLAGS_verbose);
    LoggingSeverity severity =
        verbose ? LS_VERBOSE : absl::GetFlag(FLAGS_log_level);
    config.set_min_severity(severity);
    config.set_debug_severity(severity);

    config.set_log_to_stderr(absl::GetFlag(FLAGS_logs) || verbose);

    InitializeLogging(std::move(config));

    // Log after InitializeLogging() so the RTC_LOG call does not trigger
    // implicit logging initialization before `config` is applied.
    if (verbose) {
      RTC_LOG(LS_WARNING)
          << "Both --verbose and --log-level= flags are specified. "
          << "Prefer using only --log-level.";
    }

    metrics::Enable();

#if defined(WEBRTC_WIN)
    winsock_init_ = std::make_unique<WinsockInitializer>();
#endif

    // Initialize SSL which are used by several tests.
    InitializeSSL();
    SSLStreamAdapter::EnableTimeCallbackForTesting();

    return 0;
  }

  int Run(int argc, char* argv[]) override {
    std::string trace_event_path = absl::GetFlag(FLAGS_trace_event);
    const bool capture_events = !trace_event_path.empty();
    if (capture_events) {
      StartTracingCapture(trace_event_path);
    }

    std::optional<std::vector<std::string>> metrics_to_plot =
        absl::GetFlag(FLAGS_plot);

    if (metrics_to_plot->empty()) {
      metrics_to_plot = std::nullopt;
    } else {
      if (metrics_to_plot->size() == 1 &&
          (*metrics_to_plot)[0] == kPlotAllMetrics) {
        metrics_to_plot->clear();
      }
    }
    // The sharding arguments take precedence over the sharding environment
    // variables.
    if (!absl::GetFlag(FLAGS_test_launcher_shard_index).empty() &&
        !absl::GetFlag(FLAGS_test_launcher_total_shards).empty()) {
      std::string shard_index =
          "GTEST_SHARD_INDEX=" + absl::GetFlag(FLAGS_test_launcher_shard_index);
      std::string total_shards =
          "GTEST_TOTAL_SHARDS=" +
          absl::GetFlag(FLAGS_test_launcher_total_shards);
      putenv(total_shards.data());
      putenv(shard_index.data());
    }

#if defined(WEBRTC_IOS)
    test::InitTestSuite(RUN_ALL_TESTS, argc, argv,
                        absl::GetFlag(FLAGS_write_perf_output_on_ios),
                        absl::GetFlag(FLAGS_webrtc_test_metrics_output_path),
                        metrics_to_plot);
    test::RunTestsFromIOSApp();
    int exit_code = 0;
#else
    int exit_code = RUN_ALL_TESTS();

    std::vector<std::unique_ptr<test::MetricsExporter>> exporters;
    exporters.push_back(std::make_unique<test::StdoutMetricsExporter>());
    if (!absl::GetFlag(FLAGS_webrtc_test_metrics_output_path).empty()) {
      exporters.push_back(std::make_unique<test::MetricsSetProtoFileExporter>(
          test::MetricsSetProtoFileExporter::Options(
              absl::GetFlag(FLAGS_webrtc_test_metrics_output_path))));
    }
    if (!absl::GetFlag(FLAGS_isolated_script_test_perf_output).empty()) {
      exporters.push_back(
          std::make_unique<test::ChromePerfDashboardMetricsExporter>(
              absl::GetFlag(FLAGS_isolated_script_test_perf_output)));
    }
    // Log number of tests that should be run, are disabled or skipped and total
    // number.
    int total_test_count =
        ::testing::UnitTest::GetInstance()->total_test_count();
    int test_to_run_count =
        ::testing::UnitTest::GetInstance()->test_to_run_count();
    int disabled_test_count =
        ::testing::UnitTest::GetInstance()->disabled_test_count();
    int skipped_test_count =
        ::testing::UnitTest::GetInstance()->skipped_test_count();
    absl::string_view test_suite_name = test::FileName(argv[0]);
    test::GetGlobalMetricsLogger()->LogSingleValueMetric(
        "TotalTestCount", test_suite_name, total_test_count, test::Unit::kCount,
        test::ImprovementDirection::kBiggerIsBetter);
    test::GetGlobalMetricsLogger()->LogSingleValueMetric(
        "RunTestCount", test_suite_name, test_to_run_count, test::Unit::kCount,
        test::ImprovementDirection::kBiggerIsBetter);
    test::GetGlobalMetricsLogger()->LogSingleValueMetric(
        "DisabledTestCount", test_suite_name, disabled_test_count,
        test::Unit::kCount, test::ImprovementDirection::kSmallerIsBetter);
    test::GetGlobalMetricsLogger()->LogSingleValueMetric(
        "SkippedTestCount", test_suite_name, skipped_test_count,
        test::Unit::kCount, test::ImprovementDirection::kSmallerIsBetter);

    test::ExportPerfMetric(*test::GetGlobalMetricsLogger(),
                           std::move(exporters));

    std::string result_filename =
        absl::GetFlag(FLAGS_isolated_script_test_output);
    if (!result_filename.empty()) {
      std::ofstream result_file(result_filename);
      result_file << "{\"version\": 3}";
    }
#endif

    if (capture_events) {
      StopTracingCapture();
    }

#if defined(ADDRESS_SANITIZER) || defined(LEAK_SANITIZER) ||  \
    defined(MEMORY_SANITIZER) || defined(THREAD_SANITIZER) || \
    defined(UNDEFINED_SANITIZER)
    // We want the test flagged as failed only for sanitizer defects,
    // in which case the sanitizer will override exit code with 66.
    exit_code = 0;
#endif

    return exit_code;
  }

  ~TestMainImpl() override = default;

 private:
#if defined(WEBRTC_WIN)
  std::unique_ptr<WinsockInitializer> winsock_init_;
#endif
#if defined(RTC_USE_PERFETTO)
  std::unique_ptr<perfetto::TracingSession> tracing_session_;
  FILE* tracing_output_file_ = nullptr;
#endif

  void StartTracingCapture(absl::string_view trace_output_file) {
#if defined(RTC_USE_PERFETTO)
    tracing_output_file_ = std::fopen(trace_output_file.data(), "w");
    if (!tracing_output_file_) {
      RTC_LOG(LS_ERROR) << "Failed to open trace file \"" << trace_output_file
                        << "\". Tracing will be disabled.";
    }
    perfetto::TracingInitArgs args;
    args.backends |= perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);
    webrtc::RegisterPerfettoTrackEvents();

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(1024);  // Record up to 1 MiB.
    tracing_session_ = perfetto::Tracing::NewTrace();
    tracing_session_->Setup(cfg);
    RTC_LOG(LS_INFO)
        << "Starting tracing with Perfetto and outputting to file \""
        << trace_output_file << "\"";
    tracing_session_->StartBlocking();
#else
    Environment env = CreateTestEnvironment();
    tracing::SetupInternalTracer(env);
    tracing::StartInternalCapture(trace_output_file);
#endif
  }

  void StopTracingCapture() {
#if defined(RTC_USE_PERFETTO)
    if (tracing_output_file_) {
      RTC_CHECK(tracing_session_);
      tracing_session_->StopBlocking();
      std::vector<char> tracing_data = tracing_session_->ReadTraceBlocking();
      size_t count = std::fwrite(tracing_data.data(), sizeof tracing_data[0],
                                 tracing_data.size(), tracing_output_file_);
      if (count != tracing_data.size()) {
        RTC_LOG(LS_ERROR) << "Expected to write " << tracing_data.size()
                          << " bytes but only " << count << " bytes written";
      }
      std::fclose(tracing_output_file_);
      tracing_output_file_ = nullptr;
    } else {
      RTC_LOG(LS_INFO) << "no file";
    }

#else
    tracing::StopInternalCapture();
#endif
  }
};

}  // namespace

std::unique_ptr<TestMain> TestMain::Create() {
  return std::make_unique<TestMainImpl>();
}

}  // namespace webrtc
