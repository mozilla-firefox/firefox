/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/create_network_emulation_manager.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/test/network_emulation_manager.h"
#include "api/test/simulated_network.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "net/dcsctp/public/dcsctp_message.h"
#include "net/dcsctp/public/dcsctp_options.h"
#include "net/dcsctp/public/dcsctp_socket.h"
#include "net/dcsctp/public/timeout.h"
#include "net/dcsctp/public/types.h"
#include "net/dcsctp/socket/dcsctp_socket.h"
#include "net/dcsctp/testing/testing_macros.h"
#include "net/dcsctp/timer/task_queue_timeout.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/random.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/strings/string_format.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/clock.h"
#include "test/gmock.h"
#include "test/gtest.h"

#if !defined(WEBRTC_ANDROID) && defined(NDEBUG) && \
    !defined(THREAD_SANITIZER) && !defined(MEMORY_SANITIZER)
#define DCSCTP_NDEBUG_TEST(t) t
#else
// In debug mode, and when MSAN or TSAN sanitizers are enabled, these tests are
// too expensive to run due to extensive consistency checks that iterate on all
// outstanding chunks. Same with low-end Android devices, which have
// difficulties with these tests.
#define DCSCTP_NDEBUG_TEST(t) DISABLED_##t
#endif

namespace dcsctp {
namespace {
using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Le;
using ::testing::SizeIs;
using ::webrtc::DataRate;
using ::webrtc::TimeDelta;
using ::webrtc::Timestamp;

constexpr StreamID kStreamId(1);
constexpr PPID kPpid(53);
constexpr size_t kSmallPayloadSize = 10;
constexpr size_t kLargePayloadSize = 10000;
constexpr size_t kHugePayloadSize = 262144;
constexpr size_t kBufferedAmountLowThreshold = kLargePayloadSize * 2;
constexpr webrtc::TimeDelta kPrintBandwidthDuration =
    webrtc::TimeDelta::Seconds(1);
constexpr webrtc::TimeDelta kBenchmarkRuntime(webrtc::TimeDelta::Seconds(10));
constexpr webrtc::TimeDelta kAWhile(webrtc::TimeDelta::Seconds(1));

inline int GetUniqueSeed() {
  static int seed = 0;
  return ++seed;
}

DcSctpOptions MakeOptionsForTest() {
  DcSctpOptions options;

  // Throughput numbers are affected by the MTU. Ensure it's constant.
  options.mtu = 1200;

  // By disabling the heartbeat interval, there will no timers at all running
  // when the socket is idle, which makes it easy to just continue the test
  // until there are no more scheduled tasks. Note that it _will_ run for longer
  // than necessary as timers aren't cancelled when they are stopped (as that's
  // not supported), but it's still simulated time and passes quickly.
  options.heartbeat_interval = DurationMs(0);
  return options;
}

// When doing throughput tests, knowing what each actor should do.
enum class ActorMode {
  kAtRest,
  kThroughputSender,
  kThroughputReceiver,
  kLimitedRetransmissionSender,
};

// An abstraction around EmulatedEndpoint, representing a bound socket that
// will send its packet to a given destination.
class BoundSocket : public webrtc::EmulatedNetworkReceiverInterface {
 public:
  void Bind(webrtc::EmulatedEndpoint* endpoint) {
    endpoint_ = endpoint;
    uint16_t port = endpoint->BindReceiver(0, this).value();
    source_address_ =
        webrtc::SocketAddress(endpoint_->GetPeerLocalAddress(), port);
  }

  void SetDestination(const BoundSocket& socket) {
    dest_address_ = socket.source_address_;
  }

  void SetReceiver(std::function<void(webrtc::CopyOnWriteBuffer)> receiver) {
    receiver_ = std::move(receiver);
  }

  void SendPacket(std::span<const uint8_t> data) {
    endpoint_->SendPacket(source_address_, dest_address_,
                          webrtc::CopyOnWriteBuffer(data));
  }

 private:
  // Implementation of `webrtc::EmulatedNetworkReceiverInterface`.
  void OnPacketReceived(webrtc::EmulatedIpPacket packet) override {
    receiver_(std::move(packet.data));
  }

  std::function<void(webrtc::CopyOnWriteBuffer)> receiver_;
  webrtc::EmulatedEndpoint* endpoint_ = nullptr;
  webrtc::SocketAddress source_address_;
  webrtc::SocketAddress dest_address_;
};

// Sends at a constant rate but with random packet sizes.
class SctpActor : public DcSctpSocketCallbacks {
 public:
  SctpActor(absl::string_view name,
            BoundSocket& emulated_socket,
            const DcSctpOptions& sctp_options,
            webrtc::Clock* absl_nonnull clock)
      : log_prefix_(std::string(name) + ": "),
        thread_(webrtc::Thread::Current()),
        emulated_socket_(emulated_socket),
        clock_(*clock),
        timeout_factory_(
            *thread_,
            [this]() { return TimeMs(Now().ms()); },
            [this](dcsctp::TimeoutID timeout_id) {
              sctp_socket_.HandleTimeout(timeout_id);
            }),
        random_(GetUniqueSeed()),
        sctp_socket_(name, *this, nullptr, sctp_options),
        last_bandwidth_printout_(Now()) {
    emulated_socket.SetReceiver([this](webrtc::CopyOnWriteBuffer buf) {
      // The receiver will be executed on the NetworkEmulation task queue, but
      // the dcSCTP socket is owned by `thread_` and is not thread-safe.
      thread_->PostTask([this, buf] { this->sctp_socket_.ReceivePacket(buf); });
    });
  }

  void PrintBandwidth() {
    Timestamp now = Now();
    TimeDelta duration = now - last_bandwidth_printout_;

    double bitrate_mbps =
        static_cast<double>(received_bytes_ * 8) / duration.ms() / 1000;
    RTC_LOG(LS_INFO) << log_prefix()
                     << webrtc::StringFormat("Received %0.2f Mbps",
                                             bitrate_mbps);

    received_bitrate_mbps_.push_back(bitrate_mbps);
    received_bytes_ = 0;
    last_bandwidth_printout_ = now;
    // Print again in a second.
    if (mode_ == ActorMode::kThroughputReceiver) {
      thread_->PostDelayedTask(
          SafeTask(safety_.flag(), [this] { PrintBandwidth(); }),
          kPrintBandwidthDuration);
    }
  }

  void SendPacket(std::span<const uint8_t> data) override {
    emulated_socket_.SendPacket(data);
  }

  std::unique_ptr<Timeout> CreateTimeout(
      webrtc::TaskQueueBase::DelayPrecision precision) override {
    return timeout_factory_.CreateTimeout(precision);
  }

  Timestamp Now() override { return clock_.CurrentTime(); }

  uint32_t GetRandomInt(uint32_t low, uint32_t high) override {
    return random_.Rand(low, high);
  }

  void OnMessageReceived(DcSctpMessage message) override {
    received_bytes_ += message.payload().size();
    last_received_message_ = std::move(message);
  }

  void OnError(ErrorKind error, absl::string_view message) override {
    RTC_LOG(LS_WARNING) << log_prefix() << "Socket error: " << ToString(error)
                        << "; " << message;
  }

  void OnAborted(ErrorKind error, absl::string_view message) override {
    RTC_LOG(LS_ERROR) << log_prefix() << "Socket abort: " << ToString(error)
                      << "; " << message;
  }

  void OnConnected() override {}

  void OnClosed() override {}

  void OnConnectionRestarted() override {}

  void OnStreamsResetFailed(std::span<const StreamID> /* outgoing_streams */,
                            absl::string_view /* reason */) override {}

  void OnStreamsResetPerformed(
      std::span<const StreamID> /* outgoing_streams */) override {}

  void OnIncomingStreamsReset(
      std::span<const StreamID> /* incoming_streams */) override {}

  void NotifyOutgoingMessageBufferEmpty() override {}

  void OnBufferedAmountLow(StreamID /* stream_id */) override {
    if (mode_ == ActorMode::kThroughputSender) {
      std::vector<uint8_t> payload(kHugePayloadSize);
      sctp_socket_.Send(DcSctpMessage(kStreamId, kPpid, std::move(payload)),
                        SendOptions());

    } else if (mode_ == ActorMode::kLimitedRetransmissionSender) {
      while (sctp_socket_.buffered_amount(kStreamId) <
             kBufferedAmountLowThreshold * 2) {
        SendOptions send_options;
        send_options.max_retransmissions = 0;
        sctp_socket_.Send(
            DcSctpMessage(kStreamId, kPpid,
                          std::vector<uint8_t>(kLargePayloadSize)),
            send_options);

        send_options.max_retransmissions = std::nullopt;
        sctp_socket_.Send(
            DcSctpMessage(kStreamId, kPpid,
                          std::vector<uint8_t>(kSmallPayloadSize)),
            send_options);
      }
    }
  }

  std::optional<DcSctpMessage> ConsumeReceivedMessage() {
    if (!last_received_message_.has_value()) {
      return std::nullopt;
    }
    DcSctpMessage ret = *std::move(last_received_message_);
    last_received_message_ = std::nullopt;
    return ret;
  }

  DcSctpSocket& sctp_socket() { return sctp_socket_; }

  void SetActorMode(ActorMode mode) {
    mode_ = mode;
    if (mode_ == ActorMode::kThroughputSender) {
      sctp_socket_.SetBufferedAmountLowThreshold(kStreamId,
                                                 kBufferedAmountLowThreshold);
      std::vector<uint8_t> payload(kHugePayloadSize);
      sctp_socket_.Send(DcSctpMessage(kStreamId, kPpid, std::move(payload)),
                        SendOptions());

    } else if (mode_ == ActorMode::kLimitedRetransmissionSender) {
      sctp_socket_.SetBufferedAmountLowThreshold(kStreamId,
                                                 kBufferedAmountLowThreshold);
      std::vector<uint8_t> payload(kHugePayloadSize);
      sctp_socket_.Send(DcSctpMessage(kStreamId, kPpid, std::move(payload)),
                        SendOptions());

    } else if (mode == ActorMode::kThroughputReceiver) {
      thread_->PostDelayedTask(
          SafeTask(safety_.flag(), [this] { PrintBandwidth(); }),
          kPrintBandwidthDuration);
    }
  }

  // Returns the average bitrate, stripping the first `remove_first_n` that
  // represent the time it took to ramp up the congestion control algorithm.
  double avg_received_bitrate_mbps(size_t remove_first_n = 3) const {
    std::vector<double> bitrates = received_bitrate_mbps_;
    bitrates.erase(bitrates.begin(), bitrates.begin() + remove_first_n);

    double sum = 0;
    for (double bitrate : bitrates) {
      sum += bitrate;
    }

    return sum / bitrates.size();
  }

 private:
  std::string log_prefix() const {
    webrtc::StringBuilder sb;
    sb << log_prefix_;
    sb << clock_.CurrentTime().ms();
    sb << ": ";
    return sb.Release();
  }

  ActorMode mode_ = ActorMode::kAtRest;
  const std::string log_prefix_;
  webrtc::Thread* thread_;
  BoundSocket& emulated_socket_;
  webrtc::Clock& clock_;
  TaskQueueTimeoutFactory timeout_factory_;
  webrtc::Random random_;
  DcSctpSocket sctp_socket_;
  size_t received_bytes_ = 0;
  std::optional<DcSctpMessage> last_received_message_;
  Timestamp last_bandwidth_printout_;
  // Per-second received bitrates, in Mbps
  std::vector<double> received_bitrate_mbps_;
  webrtc::ScopedTaskSafety safety_;
};

class DcSctpSocketNetworkTest : public testing::Test {
 protected:
  DcSctpSocketNetworkTest()
      : options_(MakeOptionsForTest()),
        emulation_(webrtc::CreateNetworkEmulationManager(
            {.time_mode = webrtc::TimeMode::kSimulated})) {}

  void MakeNetwork(const webrtc::BuiltInNetworkBehaviorConfig& config) {
    webrtc::EmulatedEndpoint* endpoint_a =
        emulation_->CreateEndpoint(webrtc::EmulatedEndpointConfig());
    webrtc::EmulatedEndpoint* endpoint_z =
        emulation_->CreateEndpoint(webrtc::EmulatedEndpointConfig());

    webrtc::EmulatedNetworkNode* node1 = emulation_->CreateEmulatedNode(config);
    webrtc::EmulatedNetworkNode* node2 = emulation_->CreateEmulatedNode(config);

    emulation_->CreateRoute(endpoint_a, {node1}, endpoint_z);
    emulation_->CreateRoute(endpoint_z, {node2}, endpoint_a);

    emulated_socket_a_.Bind(endpoint_a);
    emulated_socket_z_.Bind(endpoint_z);

    emulated_socket_a_.SetDestination(emulated_socket_z_);
    emulated_socket_z_.SetDestination(emulated_socket_a_);
  }

  void Sleep(webrtc::TimeDelta duration) {
    // Sleep in one-millisecond increments, to let timers expire when expected.
    for (int i = 0; i < duration.ms(); ++i) {
      emulation_->time_controller()->AdvanceTime(webrtc::TimeDelta::Millis(1));
    }
  }

  DcSctpOptions options_;
  std::unique_ptr<webrtc::NetworkEmulationManager> emulation_;
  BoundSocket emulated_socket_a_;
  BoundSocket emulated_socket_z_;
};

TEST_F(DcSctpSocketNetworkTest, CanConnectAndShutdown) {
  webrtc::BuiltInNetworkBehaviorConfig pipe_config;
  MakeNetwork(pipe_config);

  SctpActor sender("A", emulated_socket_a_, options_,
                   emulation_->time_controller()->GetClock());
  SctpActor receiver("Z", emulated_socket_z_, options_,
                     emulation_->time_controller()->GetClock());
  EXPECT_THAT(sender.sctp_socket().state(), SocketState::kClosed);

  sender.sctp_socket().Connect();
  Sleep(kAWhile);
  EXPECT_THAT(sender.sctp_socket().state(), SocketState::kConnected);

  sender.sctp_socket().Shutdown();
  Sleep(kAWhile);
  EXPECT_THAT(sender.sctp_socket().state(), SocketState::kClosed);
}

TEST_F(DcSctpSocketNetworkTest, CanSendLargeMessage) {
  webrtc::BuiltInNetworkBehaviorConfig pipe_config;
  pipe_config.queue_delay_ms = 30;
  MakeNetwork(pipe_config);

  SctpActor sender("A", emulated_socket_a_, options_,
                   emulation_->time_controller()->GetClock());
  SctpActor receiver("Z", emulated_socket_z_, options_,
                     emulation_->time_controller()->GetClock());
  sender.sctp_socket().Connect();

  constexpr size_t kPayloadSize = 100 * 1024;

  std::vector<uint8_t> payload(kPayloadSize);
  sender.sctp_socket().Send(DcSctpMessage(kStreamId, kPpid, payload),
                            SendOptions());

  Sleep(kAWhile);

  ASSERT_HAS_VALUE_AND_ASSIGN(DcSctpMessage message,
                              receiver.ConsumeReceivedMessage());

  EXPECT_THAT(message.payload(), SizeIs(kPayloadSize));

  sender.sctp_socket().Shutdown();
  Sleep(kAWhile);
}

TEST_F(DcSctpSocketNetworkTest, CanSendMessagesReliablyWithLowBandwidth) {
  webrtc::BuiltInNetworkBehaviorConfig pipe_config;
  pipe_config.queue_delay_ms = 30;
  pipe_config.link_capacity = DataRate::KilobitsPerSec(1000);
  MakeNetwork(pipe_config);

  SctpActor sender("A", emulated_socket_a_, options_,
                   emulation_->time_controller()->GetClock());
  SctpActor receiver("Z", emulated_socket_z_, options_,
                     emulation_->time_controller()->GetClock());
  sender.sctp_socket().Connect();

  sender.SetActorMode(ActorMode::kThroughputSender);
  receiver.SetActorMode(ActorMode::kThroughputReceiver);

  Sleep(kBenchmarkRuntime);
  sender.SetActorMode(ActorMode::kAtRest);
  receiver.SetActorMode(ActorMode::kAtRest);

  Sleep(kAWhile);

  sender.sctp_socket().Shutdown();

  Sleep(kAWhile);

  // Verify that the bitrates are in the range of 0.5-1.0 Mbps.
  double bitrate = receiver.avg_received_bitrate_mbps();
  EXPECT_THAT(bitrate, AllOf(Ge(0.5), Le(1.0)));
}

TEST_F(DcSctpSocketNetworkTest,
       DCSCTP_NDEBUG_TEST(CanSendMessagesReliablyWithMediumBandwidth)) {
  webrtc::BuiltInNetworkBehaviorConfig pipe_config;
  pipe_config.queue_delay_ms = 30;
  pipe_config.link_capacity = DataRate::KilobitsPerSec(18000);
  MakeNetwork(pipe_config);

  SctpActor sender("A", emulated_socket_a_, options_,
                   emulation_->time_controller()->GetClock());
  SctpActor receiver("Z", emulated_socket_z_, options_,
                     emulation_->time_controller()->GetClock());
  sender.sctp_socket().Connect();

  sender.SetActorMode(ActorMode::kThroughputSender);
  receiver.SetActorMode(ActorMode::kThroughputReceiver);

  Sleep(kBenchmarkRuntime);
  sender.SetActorMode(ActorMode::kAtRest);
  receiver.SetActorMode(ActorMode::kAtRest);

  Sleep(kAWhile);

  sender.sctp_socket().Shutdown();

  Sleep(kAWhile);

  // Verify that the bitrates are in the range of 16-18 Mbps.
  double bitrate = receiver.avg_received_bitrate_mbps();
  EXPECT_THAT(bitrate, AllOf(Ge(16), Le(18)));
}

TEST_F(DcSctpSocketNetworkTest, CanSendMessagesReliablyWithMuchPacketLoss) {
  webrtc::BuiltInNetworkBehaviorConfig config;
  config.queue_delay_ms = 30;
  config.loss_percent = 1;
  MakeNetwork(config);

  SctpActor sender("A", emulated_socket_a_, options_,
                   emulation_->time_controller()->GetClock());
  SctpActor receiver("Z", emulated_socket_z_, options_,
                     emulation_->time_controller()->GetClock());
  sender.sctp_socket().Connect();

  sender.SetActorMode(ActorMode::kThroughputSender);
  receiver.SetActorMode(ActorMode::kThroughputReceiver);

  Sleep(kBenchmarkRuntime);
  sender.SetActorMode(ActorMode::kAtRest);
  receiver.SetActorMode(ActorMode::kAtRest);

  Sleep(kAWhile);

  sender.sctp_socket().Shutdown();

  Sleep(kAWhile);

  // TCP calculator gives: 1200 MTU, 60ms RTT and 1% packet loss -> 1.6Mbps.
  // This test is doing slightly better (doesn't have any additional header
  // overhead etc). Verify that the bitrates are in the range of 1.5-2.5 Mbps.
  double bitrate = receiver.avg_received_bitrate_mbps();
  EXPECT_THAT(bitrate, AllOf(Ge(1.5), Le(2.5)));
}

TEST_F(DcSctpSocketNetworkTest, DCSCTP_NDEBUG_TEST(HasHighBandwidth)) {
  webrtc::BuiltInNetworkBehaviorConfig pipe_config;
  pipe_config.queue_delay_ms = 30;
  MakeNetwork(pipe_config);

  SctpActor sender("A", emulated_socket_a_, options_,
                   emulation_->time_controller()->GetClock());
  SctpActor receiver("Z", emulated_socket_z_, options_,
                     emulation_->time_controller()->GetClock());
  sender.sctp_socket().Connect();

  sender.SetActorMode(ActorMode::kThroughputSender);
  receiver.SetActorMode(ActorMode::kThroughputReceiver);

  Sleep(kBenchmarkRuntime);

  sender.SetActorMode(ActorMode::kAtRest);
  receiver.SetActorMode(ActorMode::kAtRest);
  Sleep(kAWhile);

  sender.sctp_socket().Shutdown();
  Sleep(kAWhile);

  // Verify that the bitrate is in the range of 540-640 Mbps
  double bitrate = receiver.avg_received_bitrate_mbps();
  EXPECT_THAT(bitrate, AllOf(Ge(520), Le(640)));
}
}  // namespace
}  // namespace dcsctp
