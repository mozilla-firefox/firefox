/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/registration.h"
#include "benchmark/state.h"
#include "benchmark/utils.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace {

// A typical video m-section, without the "a=mid" line.
constexpr char kVideoMediaSection[] =
    "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99 100 101\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:4ZcD\r\n"
    "a=ice-pwd:Ozxk6tlZr5dqE1ZeYvWbSVMd\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 7B:8B:F0:65:5F:78:E2:51:3B:AC:6F:F3:3F:46:1B:35:DC:"
    "B8:5F:64:1A:24:C2:43:F0:A1:58:D0:A1:2C:19:08\r\n"
    "a=setup:actpass\r\n"
    "a=extmap:1 urn:ietf:params:rtp-hdrext:toffset\r\n"
    "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
    "a=extmap:3 urn:3gpp:video-orientation\r\n"
    "a=extmap:4 http://www.ietf.org/id/"
    "draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
    "a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid\r\n"
    "a=sendrecv\r\n"
    "a=msid:stream_id track_id\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:96 VP8/90000\r\n"
    "a=rtcp-fb:96 goog-remb\r\n"
    "a=rtcp-fb:96 transport-cc\r\n"
    "a=rtcp-fb:96 ccm fir\r\n"
    "a=rtcp-fb:96 nack\r\n"
    "a=rtcp-fb:96 nack pli\r\n"
    "a=rtpmap:97 rtx/90000\r\n"
    "a=fmtp:97 apt=96\r\n"
    "a=rtpmap:98 VP9/90000\r\n"
    "a=rtcp-fb:98 goog-remb\r\n"
    "a=rtcp-fb:98 transport-cc\r\n"
    "a=rtcp-fb:98 ccm fir\r\n"
    "a=rtcp-fb:98 nack\r\n"
    "a=rtcp-fb:98 nack pli\r\n"
    "a=fmtp:98 profile-id=0\r\n"
    "a=rtpmap:99 rtx/90000\r\n"
    "a=fmtp:99 apt=98\r\n"
    "a=rtpmap:100 red/90000\r\n"
    "a=rtpmap:101 ulpfec/90000\r\n"
    "a=ssrc-group:FID 1001 1002\r\n"
    "a=ssrc:1001 cname:Xa8xD5tGMDNxQ0Ze\r\n"
    "a=ssrc:1001 msid:stream_id track_id\r\n"
    "a=ssrc:1002 cname:Xa8xD5tGMDNxQ0Ze\r\n"
    "a=ssrc:1002 msid:stream_id track_id\r\n";

// Returns an SDP with `num_media_sections` video m-sections.
std::string MakeSdp(int64_t num_media_sections) {
  std::string sdp =
      "v=0\r\n"
      "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=extmap-allow-mixed\r\n"
      "a=msid-semantic: WMS stream_id\r\n";
  for (int64_t mid = 0; mid < num_media_sections; ++mid) {
    absl::StrAppend(&sdp, kVideoMediaSection, "a=mid:", mid, "\r\n");
  }
  return sdp;
}

// Formats each log line with LogLineRef::DefaultLogLine(), like the in-tree
// sinks do, and discards it.
class DiscardingLogSink : public LogSink {
 public:
  void OnLogMessage(const LogLineRef& line) override {
    benchmark::DoNotOptimize(line.DefaultLogLine().data());
  }
};

// Registers a DiscardingLogSink for verbose logs during its lifetime. Verbose
// logs are not written to the debug output (stderr) by default.
class ScopedDiscardingLogSink {
 public:
  ScopedDiscardingLogSink() { LogMessage::AddLogToStream(&sink_, LS_VERBOSE); }
  ~ScopedDiscardingLogSink() { LogMessage::RemoveLogToStream(&sink_); }

 private:
  DiscardingLogSink sink_;
};

// Reinitializes logging with a DiscardingLogSink as a LoggingConfig sink for
// verbose logs, and restores the previous configuration when destroyed. Must
// not be used while other threads are logging.
class ScopedConfigDiscardingLogSink {
 public:
  ScopedConfigDiscardingLogSink() : saved_config_(ResetLoggingForTesting()) {
    LoggingConfig config;
    config.set_min_severity(LS_VERBOSE);
    config.AddSink(std::make_unique<DiscardingLogSink>());
    RTC_CHECK(InitializeLogging(std::move(config)));
  }
  ~ScopedConfigDiscardingLogSink() {
    ResetLoggingForTesting(std::move(saved_config_));
  }

 private:
  LoggingConfig saved_config_;
};

void LogRtpPacket(int64_t i) {
  RTC_LOG(LS_VERBOSE) << "Received RTP packet seq=" << i
                      << " timestamp=" << (i * 3000) << " ssrc=" << 0x12345678u
                      << " payload_size=" << 1200;
}

void BM_RtcLogLiteralsAndNumbers(benchmark::State& state) {
  ScopedDiscardingLogSink sink;
  int64_t i = 0;
  for (auto s : state) {
    RTC_LOG(LS_VERBOSE) << "Received RTP packet seq=" << i
                        << " timestamp=" << (i * 3000)
                        << " ssrc=" << 0x12345678u << " payload_size=" << 1200;
    ++i;
  }
}

// Same message as BM_RtcLogLiteralsAndNumbers, sent to two sinks that both use
// the default log line, e.g. a file sink and a sink that forwards to the app.
void BM_RtcLogToTwoSinks(benchmark::State& state) {
  ScopedDiscardingLogSink sink1;
  ScopedDiscardingLogSink sink2;
  int64_t i = 0;
  for (auto _ : state) {
    RTC_LOG(LS_VERBOSE) << "Received RTP packet seq=" << i
                        << " timestamp=" << (i * 3000)
                        << " ssrc=" << 0x12345678u << " payload_size=" << 1200;
    ++i;
  }
}

// Logs to a sink that is added at runtime with LogMessage::AddLogToStream(),
// from one or more threads. The first thread adds the sink before, and removes
// it after, the timed loop, which all threads enter and leave together.
void BM_RtcLogToDynamicSink(benchmark::State& state) {
  std::optional<ScopedDiscardingLogSink> sink;
  if (state.thread_index() == 0) {
    sink.emplace();
  }
  int64_t i = 0;
  for (auto s : state) {
    LogRtpPacket(i++);
  }
}

// Same as BM_RtcLogToDynamicSink, with a sink added with
// LoggingConfig::AddSink() instead.
void BM_RtcLogToConfigSink(benchmark::State& state) {
  std::optional<ScopedConfigDiscardingLogSink> sink;
  if (state.thread_index() == 0) {
    sink.emplace();
  }
  int64_t i = 0;
  for (auto s : state) {
    LogRtpPacket(i++);
  }
}

void BM_RtcLogMixedStrings(benchmark::State& state) {
  ScopedDiscardingLogSink sink;
  const std::string ufrag = "4ZcD";
  const std::string candidate =
      "Cand[:1052210311:1:udp:2122260223:192.168.1.100:61922:host::0:4ZcD:"
      "Ozxk6tlZr5dqE1ZeYvWbSVMd:1:1:0]";
  const absl::string_view candidate_str = candidate;
  int64_t i = 0;
  for (auto s : state) {
    RTC_LOG(LS_VERBOSE) << " ice_ufrag=" << ufrag
                        << " candidate=" << candidate_str << " component=" << i
                        << " cost=" << 50;
    ++i;
  }
}

// Logs a complete SDP, like PeerConnectionTestWrapper::SetLocalDescription()
// does.
void BM_RtcLogSdp(benchmark::State& state) {
  ScopedDiscardingLogSink sink;
  const std::string name = "caller";
  const absl::string_view type = "offer";
  const std::string sdp = MakeSdp(state.range(0));
  for (auto s : state) {
    RTC_LOG(LS_VERBOSE) << "PeerConnectionTestWrapper " << name
                        << ": SetLocalDescription " << type << " " << sdp;
  }
  state.counters["sdp_bytes"] = static_cast<double>(sdp.size());
}

BENCHMARK(BM_RtcLogLiteralsAndNumbers);
BENCHMARK(BM_RtcLogToTwoSinks);
BENCHMARK(BM_RtcLogToDynamicSink)->ThreadRange(1, 8);
BENCHMARK(BM_RtcLogToConfigSink)->ThreadRange(1, 8);
BENCHMARK(BM_RtcLogMixedStrings);
// The argument is the number of m-sections in the SDP.
BENCHMARK(BM_RtcLogSdp)->Arg(1)->Arg(4)->Arg(16);

}  // namespace
}  // namespace webrtc
