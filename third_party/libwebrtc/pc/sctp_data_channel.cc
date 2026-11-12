/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/sctp_data_channel.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/data_channel_interface.h"
#include "api/make_ref_counted.h"
#include "api/priority.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/data_channel_transport_interface.h"
#include "pc/data_channel_utils.h"
#include "pc/proxy.h"
#include "pc/sctp_utils.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/weak_ptr.h"

namespace webrtc {

namespace {

size_t kMaxQueuedReceivedDataBytes = 16 * 1024 * 1024;

std::atomic<int> g_unique_id{0};

int GenerateUniqueId() {
  return ++g_unique_id;
}

// Define proxy for DataChannelInterface.
BEGIN_PROXY_MAP(DataChannel)
PROXY_PRIMARY_THREAD_DESTRUCTOR()
BYPASS_PROXY_METHOD1(void, RegisterObserver, DataChannelObserver*)
BYPASS_PROXY_METHOD0(void, UnregisterObserver)
BYPASS_PROXY_CONSTMETHOD0(std::string, label)
BYPASS_PROXY_CONSTMETHOD0(bool, reliable)
BYPASS_PROXY_CONSTMETHOD0(bool, ordered)
BYPASS_PROXY_CONSTMETHOD0(std::optional<int>, maxRetransmitsOpt)
BYPASS_PROXY_CONSTMETHOD0(std::optional<int>, maxPacketLifeTime)
BYPASS_PROXY_CONSTMETHOD0(std::string, protocol)
BYPASS_PROXY_CONSTMETHOD0(bool, negotiated)
BYPASS_PROXY_CONSTMETHOD0(int, id)
BYPASS_PROXY_CONSTMETHOD0(PriorityValue, priority)
BYPASS_PROXY_CONSTMETHOD0(DataState, state)
BYPASS_PROXY_CONSTMETHOD0(RTCError, error)
PROXY_SECONDARY_CONSTMETHOD0(uint32_t, messages_sent)
PROXY_SECONDARY_CONSTMETHOD0(uint64_t, bytes_sent)
PROXY_SECONDARY_CONSTMETHOD0(uint32_t, messages_received)
PROXY_SECONDARY_CONSTMETHOD0(uint64_t, bytes_received)
PROXY_SECONDARY_CONSTMETHOD0(uint64_t, buffered_amount)
PROXY_SECONDARY_METHOD0(void, Close)
PROXY_SECONDARY_METHOD1(bool, Send, const DataBuffer&)
BYPASS_PROXY_METHOD2(void,
                     SendAsync,
                     DataBuffer,
                     absl::AnyInvocable<void(RTCError) &&>)
END_PROXY_MAP(DataChannel)
}  // namespace

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
InternalDataChannelInit::InternalDataChannelInit(const DataChannelInit& base)
    : DataChannelInit(base), open_handshake_role(kOpener) {
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  // If the channel is externally negotiated, do not send the OPEN message.
  if (base.negotiated) {
    open_handshake_role = kNone;
  } else {
    // Datachannel is externally negotiated. Ignore the id value.
    // Specified in createDataChannel, WebRTC spec section 6.1 bullet 13.
    id = -1;
  }
  // Backwards compatibility: If maxRetransmits or maxRetransmitTime
  // are negative, the feature is not enabled.
  // Values are clamped to a 16bit range.
  if (maxRetransmits) {
    if (*maxRetransmits < 0) {
      RTC_LOG(LS_ERROR)
          << "Accepting maxRetransmits < 0 for backwards compatibility";
      maxRetransmits = std::nullopt;
    } else if (*maxRetransmits > std::numeric_limits<uint16_t>::max()) {
      maxRetransmits = std::numeric_limits<uint16_t>::max();
    }
  }

  if (maxRetransmitTime) {
    if (*maxRetransmitTime < 0) {
      RTC_LOG(LS_ERROR)
          << "Accepting maxRetransmitTime < 0 for backwards compatibility";
      maxRetransmitTime = std::nullopt;
    } else if (*maxRetransmitTime > std::numeric_limits<uint16_t>::max()) {
      maxRetransmitTime = std::numeric_limits<uint16_t>::max();
    }
  }
}

bool InternalDataChannelInit::IsValid() const {
  if (id < -1)
    return false;

  if (maxRetransmits.has_value() && maxRetransmits.value() < 0)
    return false;

  if (maxRetransmitTime.has_value() && maxRetransmitTime.value() < 0)
    return false;

  // Only one of these can be set.
  if (maxRetransmits.has_value() && maxRetransmitTime.has_value())
    return false;

  return true;
}

std::optional<StreamId> SctpSidAllocator::AllocateSid(SSLRole role) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  int potential_sid = (role == SSL_CLIENT) ? 0 : 1;
  while (potential_sid <= max_sid_) {
    StreamId sid(potential_sid);
    if (used_sids_.insert(sid).second)
      return sid;
    potential_sid += 2;
  }
  RTC_LOG(LS_ERROR) << "SCTP sid allocation pool exhausted.";
  return std::nullopt;
}

bool SctpSidAllocator::ReserveSid(StreamId sid) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return used_sids_.insert(sid).second;
}

void SctpSidAllocator::ReleaseSid(StreamId sid) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  used_sids_.erase(sid);
}

struct SctpDataChannel::CachedState {
  int id;
  DataChannelInterface::DataState state;
  RTCError error;
};

// A DataChannelObserver implementation that offers backwards compatibility with
// implementations that aren't yet ready to be called back on the network
// thread. This implementation posts events to the signaling thread where
// events are delivered.
// In the class, and together with the `SctpDataChannel` implementation, there's
// special handling for the `state()` property whereby if that property is
// queried on the channel object while inside an event callback, we return
// the state that was active at the time the event was issued. This is to avoid
// a problem with calling the `state()` getter on the proxy, which would do
// a blocking call to the network thread, effectively flushing operations on
// the network thread that could cause the state to change and eventually return
// a misleading or arguably, wrong, state value to the callback implementation.
// As a future improvement to the ObserverAdapter, we could do the same for
// other properties that need to be read on the network thread. Eventually
// all implementations should expect to be called on the network thread though
// and the ObserverAdapter no longer be necessary.
class SctpDataChannel::ObserverAdapter : public DataChannelObserver {
 public:
  ObserverAdapter(SctpDataChannel* channel, DataChannelObserver* delegate)
      : delegate_(delegate),
        channel_(channel),
        controller_safety_(channel->controller_safety_) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    RTC_DCHECK(delegate_);
  }

 private:
  void OnStateChange() override {
    RTC_DCHECK_RUN_ON(network_thread());
    RTC_DCHECK_EQ(signaling_thread(), channel_->signaling_thread_);
    channel_->CacheStateAndCallBackOnSignalingThread(
        SafeTask(safety_.flag(), [this] {
          RTC_DCHECK_RUN_ON(signaling_thread());
          if (controller_safety_->alive()) {
            delegate_->OnStateChange();
          }
        }));
  }

  void OnMessage(const DataBuffer& buffer) override {
    RTC_DCHECK_RUN_ON(network_thread());
    channel_->CacheStateAndCallBackOnSignalingThread(
        SafeTask(safety_.flag(), [this, buffer = buffer] {
          RTC_DCHECK_RUN_ON(signaling_thread());
          if (controller_safety_->alive()) {
            delegate_->OnMessage(buffer);
          }
        }));
  }

  void OnBufferedAmountChange(uint64_t sent_data_size) override {
    RTC_DCHECK_RUN_ON(network_thread());
    channel_->CacheStateAndCallBackOnSignalingThread(
        SafeTask(safety_.flag(), [this, sent_data_size] {
          RTC_DCHECK_RUN_ON(signaling_thread());
          if (controller_safety_->alive()) {
            delegate_->OnBufferedAmountChange(sent_data_size);
          }
        }));
  }

  bool IsOkToCallOnTheNetworkThread() override { return true; }

  Thread* signaling_thread() const { return signaling_thread_; }
  Thread* network_thread() const { return network_thread_; }

  DataChannelObserver* const delegate_ RTC_GUARDED_BY(signaling_thread());
  SctpDataChannel* const channel_;
  // Keep a copy of controller_safety_ to safely check PC alive state on the
  // signaling thread without dereferencing `channel_`, which might be deleted.
  const scoped_refptr<PendingTaskSafetyFlag> controller_safety_;
  // Make sure to keep our own signaling_thread_ pointer to avoid dereferencing
  // `channel_` in the `RTC_DCHECK_RUN_ON` checks on the signaling thread.
  Thread* const signaling_thread_{channel_->signaling_thread_};
  Thread* const network_thread_{channel_->network_thread_};
  ScopedTaskSafety safety_;
};

// static
absl_nonnull scoped_refptr<SctpDataChannel> SctpDataChannel::Create(
    WeakPtr<SctpDataChannelControllerInterface> controller,
    absl::string_view label,
    bool connected_to_transport,
    const InternalDataChannelInit& config,
    std::optional<int> max_message_size,
    scoped_refptr<PendingTaskSafetyFlag> controller_safety,
    Thread* signaling_thread,
    Thread* network_thread) {
  RTC_DCHECK(config.IsValid());
  return make_ref_counted<SctpDataChannel>(
      config, std::move(controller), label, connected_to_transport,
      max_message_size, std::move(controller_safety), signaling_thread,
      network_thread);
}

// static
absl_nonnull scoped_refptr<DataChannelInterface> SctpDataChannel::CreateProxy(
    scoped_refptr<SctpDataChannel> channel) {
  // Copy thread params to local variables before `std::move()`.
  auto* signaling_thread = channel->signaling_thread_;
  auto* network_thread = channel->network_thread_;
  return DataChannelProxy::Create(signaling_thread, network_thread,
                                  std::move(channel));
}

SctpDataChannel::SctpDataChannel(
    const InternalDataChannelInit& config,
    WeakPtr<SctpDataChannelControllerInterface> controller,
    absl::string_view label,
    bool connected_to_transport,
    std::optional<int> max_message_size,
    scoped_refptr<PendingTaskSafetyFlag> controller_safety,
    Thread* signaling_thread,
    Thread* network_thread)
    : signaling_thread_(signaling_thread),
      network_thread_(network_thread),
      id_n_(config.id == -1 ? std::nullopt : std::make_optional(config.id)),
      internal_id_(GenerateUniqueId()),
      label_(label),
      protocol_(config.protocol),
      max_retransmit_time_(config.maxRetransmitTime),
      max_retransmits_(config.maxRetransmits),
      priority_(config.priority),
      negotiated_(config.negotiated),
      ordered_(config.ordered),
      observer_(nullptr),
      max_message_size_(max_message_size),
      controller_(std::move(controller)),
      controller_safety_(std::move(controller_safety)) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // Since we constructed on the network thread we can't (yet) check the
  // `controller_` pointer since doing so will trigger a thread check.
  RTC_DCHECK(config.IsValid());

  if (connected_to_transport) {
    network_safety_->SetAlive();
  }

  switch (config.open_handshake_role) {
    case InternalDataChannelInit::kNone:  // pre-negotiated
      handshake_state_ = kHandshakeReady;
      break;
    case InternalDataChannelInit::kOpener:
      handshake_state_ = kHandshakeShouldSendOpen;
      break;
    case InternalDataChannelInit::kAcker:
      handshake_state_ = kHandshakeShouldSendAck;
      break;
  }
}

SctpDataChannel::~SctpDataChannel() = default;

void SctpDataChannel::RegisterObserver(DataChannelObserver* observer) {
  // Note: at this point, we do not know on which thread we're being called
  // from since this method bypasses the proxy. On Android in particular,
  // registration methods are called from unknown threads.
  RTC_DCHECK(observer);

  // Check if we should set up an observer adapter that will make sure that
  // callbacks are delivered on the signaling thread rather than directly
  // on the network thread.
  const auto* current_thread = Thread::Current();
  // TODO(webrtc:11547): Eventually all DataChannelObserver implementations
  // should be called on the network thread and IsOkToCallOnTheNetworkThread().
  if (!observer->IsOkToCallOnTheNetworkThread()) {
    RTC_LOG(LS_WARNING) << "DataChannelObserver - adapter needed";
    auto prepare_observer = [&]() {
      RTC_DCHECK_RUN_ON(signaling_thread_);
      observer_adapter_ = std::make_unique<ObserverAdapter>(this, observer);
      return observer_adapter_.get();
    };
    // Instantiate the adapter in the right context and then substitute the
    // observer pointer the SctpDataChannel will call back on, with the adapter.
    if (signaling_thread_ == current_thread) {
      observer = prepare_observer();
    } else {
      observer = signaling_thread_->BlockingCall(std::move(prepare_observer));
    }
  }

  auto register_observer = [this, observer] {
    RTC_DCHECK_RUN_ON(network_thread_);
    observer_ = observer;
    if (max_message_size_) {
      observer_->OnMaxMessageSize(*max_message_size_);
    }
    DeliverQueuedReceivedData();
  };

  if (network_thread_ == current_thread) {
    register_observer();
  } else {
    network_thread_->BlockingCall(std::move(register_observer));
  }
}

void SctpDataChannel::UnregisterObserver() {
  // Note: As with `RegisterObserver`, the proxy is being bypassed.
  const auto* current_thread = Thread::Current();
  // Callers must not be invoking the unregistration from the network thread
  // (assuming a multi-threaded environment where we have a dedicated network
  // thread). That would indicate non-network related work happening on the
  // network thread or that unregistration is being done from within a callback
  // (without unwinding the stack, which is a requirement).
  // The network thread is not allowed to make blocking calls to the signaling
  // thread, so that would blow up if attempted. Since we support an adapter
  // for observers that are not safe to call on the network thread, we do
  // need to check+free it on the signaling thread.
  RTC_DCHECK(current_thread != network_thread_ ||
             network_thread_ == signaling_thread_);

  auto unregister_observer = [&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    observer_ = nullptr;
  };

  if (current_thread == network_thread_) {
    unregister_observer();
  } else {
    network_thread_->BlockingCall(std::move(unregister_observer));
  }

  auto clear_observer = [&]() {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    observer_adapter_.reset();
  };

  if (current_thread != signaling_thread_) {
    signaling_thread_->BlockingCall(std::move(clear_observer));
  } else {
    clear_observer();
  }
}

std::string SctpDataChannel::label() const {
  return label_;
}

bool SctpDataChannel::reliable() const {
  // May be called on any thread.
  return !max_retransmits_ && !max_retransmit_time_;
}

bool SctpDataChannel::ordered() const {
  return ordered_;
}

std::optional<int> SctpDataChannel::maxPacketLifeTime() const {
  return max_retransmit_time_;
}

std::optional<int> SctpDataChannel::maxRetransmitsOpt() const {
  return max_retransmits_;
}

std::string SctpDataChannel::protocol() const {
  return protocol_;
}

bool SctpDataChannel::negotiated() const {
  return negotiated_;
}

int SctpDataChannel::id() const {
  const auto* current_thread = Thread::Current();
  if (current_thread == signaling_thread_) {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    if (cached_state_) {
      return cached_state_->id;
    }
  }

  auto return_id = [&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    return id_n_.has_value() ? id_n_->stream_id_int() : -1;
  };

  return current_thread == network_thread_
             ? return_id()
             : network_thread_->BlockingCall(std::move(return_id));
}

PriorityValue SctpDataChannel::priority() const {
  return priority_.value_or(PriorityValue(Priority::kLow));
}

uint64_t SctpDataChannel::buffered_amount() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (controller_ != nullptr && id_n_.has_value()) {
    return controller_->buffered_amount(*id_n_);
  }
  return 0u;
}

void SctpDataChannel::Close() {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (state_ == kClosing || state_ == kClosed)
    return;
  SetState(kClosing);
  // Will send queued data before beginning the underlying closing procedure.
  UpdateState();
}

SctpDataChannel::DataState SctpDataChannel::state() const {
  // Note: The proxy is bypassed for the `state()` accessor. This is to allow
  // observer callbacks to query what the new state is from within a state
  // update notification without having to do a blocking call to the network
  // thread from within a callback. This also makes it so that the returned
  // state is guaranteed to be the new state that provoked the state change
  // notification, whereby a blocking call to the network thread might end up
  // getting put behind other messages on the network thread and eventually
  // fetch a different state value (since pending messages might cause the
  // state to change in the meantime).
  const auto* current_thread = Thread::Current();
  if (current_thread == signaling_thread_) {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    if (cached_state_) {
      return cached_state_->state;
    }
  }

  auto return_state = [&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    return state_;
  };

  return current_thread == network_thread_
             ? return_state()
             : network_thread_->BlockingCall(std::move(return_state));
}

RTCError SctpDataChannel::error() const {
  const auto* current_thread = Thread::Current();
  if (current_thread == signaling_thread_) {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    if (cached_state_) {
      return cached_state_->error;
    }
  }

  auto return_error = [&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    return error_;
  };

  return current_thread == network_thread_
             ? return_error()
             : network_thread_->BlockingCall(std::move(return_error));
}

uint32_t SctpDataChannel::messages_sent() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  return messages_sent_;
}

uint64_t SctpDataChannel::bytes_sent() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  return bytes_sent_;
}

uint32_t SctpDataChannel::messages_received() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  return messages_received_;
}

uint64_t SctpDataChannel::bytes_received() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  return bytes_received_;
}

bool SctpDataChannel::Send(const DataBuffer& buffer) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTCError err = SendImpl(buffer);
  if (!err.ok() && err.type() != RTCErrorType::INVALID_STATE &&
      err.type() != RTCErrorType::RESOURCE_EXHAUSTED &&
      err.type() != RTCErrorType::NETWORK_ERROR) {
    RTC_LOG(LS_INFO) << "Unexpected error code: " << err;
  }
  return err.ok();
}

// RTC_RUN_ON(network_thread_);
RTCError SctpDataChannel::SendImpl(DataBuffer buffer) {
  // The caller increases the cached `bufferedAmount` even if there are errors.
  expected_buffer_amount_ += buffer.size();

  if (state_ != kOpen) {
    error_ = RTCError(RTCErrorType::INVALID_STATE);
    return error_;
  }

  return SendDataMessage(buffer, true);
}

void SctpDataChannel::SendAsync(
    DataBuffer buffer,
    absl::AnyInvocable<void(RTCError) &&> on_complete) {
  // Note: at this point, we do not know on which thread we're being called
  // since this method bypasses the proxy. On Android the thread might be VM
  // owned, on other platforms it might be the signaling thread, or in Chrome
  // it can be the JS thread. We also don't know if it's consistently the same
  // thread. So we always post to the network thread (even if the current thread
  // might be the network thread - in theory a call could even come from within
  // the `on_complete` callback).
  network_thread_->PostTask([this, network_safety = network_safety_,
                             buffer = std::move(buffer),
                             on_complete = std::move(on_complete)]() mutable {
    // Once closed, the channel may already have been deleted on the signaling
    // thread, so only touch `this` while `network_safety` is alive.
    RTCError err(RTCErrorType::INVALID_STATE);
    if (network_safety->alive()) {
      RTC_DCHECK_RUN_ON(network_thread_);
      err = SendImpl(std::move(buffer));
    }
    if (on_complete)
      std::move(on_complete)(std::move(err));
  });
}

void SctpDataChannel::SetSctpSid_n(StreamId sid) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(!id_n_.has_value());
  RTC_DCHECK_NE(handshake_state_, kHandshakeWaitingForAck);
  RTC_DCHECK_EQ(state_, kConnecting);
  id_n_ = sid;
}

void SctpDataChannel::OnClosingProcedureStartedRemotely() {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (state_ != kClosing && state_ != kClosed) {
    // Don't bother sending queued data since the side that initiated the
    // closure wouldn't receive it anyway. See crbug.com/559394 for a lengthy
    // discussion about this.

    // Note that this is handled by the SctpTransport, when an incoming stream
    // reset notification comes in, the outgoing stream is closed, which
    // discards data.

    // Just need to change state to kClosing, SctpTransport will handle the
    // rest of the closing procedure and OnClosingProcedureComplete will be
    // called later.
    started_closing_procedure_ = true;
    SetState(kClosing);
  }
}

void SctpDataChannel::OnClosingProcedureComplete() {
  RTC_DCHECK_RUN_ON(network_thread_);
  // If the closing procedure is complete, we should have finished sending
  // all pending data and transitioned to kClosing already.
  RTC_DCHECK_EQ(state_, kClosing);
  if (controller_ && id_n_.has_value()) {
    RTC_DCHECK_EQ(controller_->buffered_amount(*id_n_), 0);
  }
  SetState(kClosed);
}

void SctpDataChannel::OnTransportChannelCreated() {
  RTC_DCHECK_RUN_ON(network_thread_);
  network_safety_->SetAlive();
}

void SctpDataChannel::OnTransportChannelClosed(RTCError error) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // The SctpTransport is unusable, which could come from multiple reasons:
  // - the SCTP m= section was rejected
  // - the DTLS transport is closed
  // - the SCTP transport is closed
  CloseAbruptlyWithError(std::move(error));
}

void SctpDataChannel::OnBufferedAmountLow() {
  RTC_DCHECK_RUN_ON(network_thread_);
  MaybeSendOnBufferedAmountChanged();

  if (state_ == DataChannelInterface::kClosing && !started_closing_procedure_ &&
      id_n_.has_value() && buffered_amount() == 0) {
    started_closing_procedure_ = true;
    controller_->RemoveSctpDataStream(*id_n_);
  }
}

void SctpDataChannel::OnMaxMessageSize(int max_message_size) {
  RTC_DCHECK_RUN_ON(network_thread_);

  max_message_size_ = max_message_size;
  if (observer_) {
    observer_->OnMaxMessageSize(max_message_size);
  }
}

DataChannelStats SctpDataChannel::GetStats() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  DataChannelStats stats{.internal_id = internal_id_,
                         .id = id(),
                         .label = label(),
                         .protocol = protocol(),
                         .state = state(),
                         .messages_sent = messages_sent(),
                         .messages_received = messages_received(),
                         .bytes_sent = bytes_sent(),
                         .bytes_received = bytes_received()};
  return stats;
}

void SctpDataChannel::CacheStateAndCallBackOnSignalingThread(
    absl::AnyInvocable<void() &&> callback) {
  RTC_DCHECK_RUN_ON(network_thread_);
  scoped_refptr<SctpDataChannel> me(this);
  signaling_thread_->PostTask(
      [me = std::move(me),
       cache = CachedState{.id = id(), .state = state_, .error = error_},
       callback = std::move(callback)]() mutable {
        RTC_DCHECK_RUN_ON(me->signaling_thread_);
        RTC_DCHECK(!me->cached_state_);
        me->cached_state_ = &cache;
        std::move(callback)();
        me->cached_state_ = nullptr;
      });
}

void SctpDataChannel::OnDataReceived(DataMessageType type,
                                     const CopyOnWriteBuffer& payload) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(id_n_.has_value());

  if (type == DataMessageType::kControl) {
    if (handshake_state_ != kHandshakeWaitingForAck) {
      // Ignore it if we are not expecting an ACK message.
      RTC_LOG(LS_WARNING)
          << "DataChannel received unexpected CONTROL message, sid = "
          << id_n_->stream_id_int();
      return;
    }
    if (ParseDataChannelOpenAckMessage(payload)) {
      // We can send unordered as soon as we receive the ACK message.
      handshake_state_ = kHandshakeReady;
      RTC_LOG(LS_INFO) << "DataChannel received OPEN_ACK message, sid = "
                       << id_n_->stream_id_int();
    } else {
      RTC_LOG(LS_WARNING)
          << "DataChannel failed to parse OPEN_ACK message, sid = "
          << id_n_->stream_id_int();
    }
    return;
  }

  RTC_DCHECK(type == DataMessageType::kBinary ||
             type == DataMessageType::kText);

  RTC_DLOG(LS_VERBOSE) << "DataChannel received DATA message, sid = "
                       << id_n_->stream_id_int();
  // We can send unordered as soon as we receive any DATA message since the
  // remote side must have received the OPEN (and old clients do not send
  // OPEN_ACK).
  if (handshake_state_ == kHandshakeWaitingForAck) {
    handshake_state_ = kHandshakeReady;
  }

  bool binary = (type == DataMessageType::kBinary);
  auto buffer = std::make_unique<DataBuffer>(payload, binary);
  if (state_ == kOpen && observer_) {
    ++messages_received_;
    bytes_received_ += buffer->size();
    observer_->OnMessage(*buffer);
  } else {
    if (queued_received_data_.byte_count() + payload.size() >
        kMaxQueuedReceivedDataBytes) {
      RTC_LOG(LS_ERROR) << "Queued received data exceeds the max buffer size.";

      queued_received_data_.Clear();
      CloseAbruptlyWithError(
          RTCError(RTCErrorType::RESOURCE_EXHAUSTED,
                   "Queued received data exceeds the max buffer size."));

      return;
    }
    queued_received_data_.PushBack(std::move(buffer));
  }
}

void SctpDataChannel::OnTransportReady() {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(connected_to_transport());
  RTC_DCHECK(id_n_.has_value());

  UpdateState();
}

void SctpDataChannel::PostOnTransportReady() {
  network_thread_->PostTask(
      SafeTask(network_safety_, [this] { OnTransportReady(); }));
}

void SctpDataChannel::CloseAbruptlyWithError(RTCError error) {
  RTC_DCHECK_RUN_ON(network_thread_);

  if (state_ == kClosed) {
    return;
  }

  network_safety_->SetNotAlive();

  // Still go to "kClosing" before "kClosed", since observers may be expecting
  // that.
  SetState(kClosing);
  error_ = std::move(error);
  SetState(kClosed);
}

void SctpDataChannel::CloseAbruptlyWithDataChannelFailure(
    const std::string& message) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTCError error(RTCErrorType::OPERATION_ERROR_WITH_DATA, message);
  error.set_error_detail(RTCErrorDetailType::DATA_CHANNEL_FAILURE);
  CloseAbruptlyWithError(std::move(error));
}

// RTC_RUN_ON(network_thread_).
void SctpDataChannel::UpdateState() {
  // UpdateState determines what to do from a few state variables. Include
  // all conditions required for each state transition here for
  // clarity. OnTransportReady(true) will send any queued data and then invoke
  // UpdateState().

  switch (state_) {
    case kConnecting: {
      if (connected_to_transport() && controller_) {
        if (handshake_state_ == kHandshakeShouldSendOpen) {
          CopyOnWriteBuffer payload;
          WriteDataChannelOpenMessage(label_, protocol_, priority_, ordered_,
                                      max_retransmits_, max_retransmit_time_,
                                      &payload);
          if (!SendControlMessage(payload)) {
            return;
          }
        } else if (handshake_state_ == kHandshakeShouldSendAck) {
          CopyOnWriteBuffer payload;
          WriteDataChannelOpenAckMessage(&payload);
          if (!SendControlMessage(payload)) {
            return;
          }
        }
        if (handshake_state_ == kHandshakeReady ||
            handshake_state_ == kHandshakeWaitingForAck) {
          SetState(kOpen);
          // If we have received buffers before the channel got writable.
          // Deliver them now.
          DeliverQueuedReceivedData();
        }
      } else {
        RTC_DCHECK(!id_n_.has_value());
      }
      break;
    }
    case kOpen: {
      break;
    }
    case kClosing: {
      if (connected_to_transport() && controller_ && id_n_.has_value()) {
        // Wait for all queued data to be sent before beginning the closing
        // procedure.
        if (controller_->buffered_amount(*id_n_) == 0) {
          // For SCTP data channels, we need to wait for the closing procedure
          // to complete; after calling RemoveSctpDataStream,
          // OnClosingProcedureComplete will end up called asynchronously
          // afterwards.
          if (!started_closing_procedure_ && id_n_.has_value()) {
            started_closing_procedure_ = true;
            controller_->RemoveSctpDataStream(*id_n_);
          }
        }
      } else {
        // When we're not connected to a transport, we'll transition
        // directly to the `kClosed` state from here.
        SetState(kClosed);
      }
      break;
    }
    case kClosed:
      break;
  }
}

// RTC_RUN_ON(network_thread_).
void SctpDataChannel::SetState(DataState state) {
  if (state_ == state) {
    return;
  }

  state_ = state;
  if (state_ == kClosed) {
    network_safety_->SetNotAlive();
  }

  if (observer_) {
    observer_->OnStateChange();
  }

  // When notified of `kClosed`, the controller hands its reference to the
  // channel over to the signaling thread, where the channel may get deleted at
  // any time. Callers must not access members after the channel has closed.
  if (controller_)
    controller_->OnChannelStateChanged(this, state_);
}

// RTC_RUN_ON(network_thread_).
void SctpDataChannel::DeliverQueuedReceivedData() {
  if (!observer_ || state_ != kOpen) {
    return;
  }

  while (!queued_received_data_.Empty()) {
    std::unique_ptr<DataBuffer> buffer = queued_received_data_.PopFront();
    ++messages_received_;
    bytes_received_ += buffer->size();
    observer_->OnMessage(*buffer);
  }
}

// RTC_RUN_ON(network_thread_)
void SctpDataChannel::MaybeSendOnBufferedAmountChanged() {
  // The `buffered_amount` in the signaling thread (RTCDataChannel in Blink)
  // has a cached variant of the SCTP socket's buffered_amount, which it
  // increases for every data sent and decreased when `OnBufferedAmountChange`
  // is sent.
  //
  // To ensure it's consistent, this object maintains its own view of that value
  // and if it changes with a reasonable amount (10kb, or down to zero), send
  // the `OnBufferedAmountChange` to update the caller's cached variable.
  if (!controller_ || !id_n_.has_value() || !observer_) {
    return;
  }

  // This becomes the resolution of how often the bufferedAmount is updated on
  // the signaling thread and exists to avoid doing cross-thread communication
  // too often. On benchmarks, Chrome handle around 300Mbps, which with this
  // size results in a rate of ~400 updates per second - a reasonable number.
  static constexpr int64_t kMinBufferedAmountDiffToTriggerCallback = 100 * 1024;
  size_t actual_buffer_amount = controller_->buffered_amount(*id_n_);
  if (actual_buffer_amount > expected_buffer_amount_) {
    RTC_DLOG(LS_ERROR) << "Actual buffer_amount larger than expected";
    return;
  }

  // Fire OnBufferedAmountChange to decrease the cached view if it represents a
  // big enough change (to reduce the frequency of cross-thread communication),
  // or if it reaches zero.
  if ((actual_buffer_amount == 0 && expected_buffer_amount_ != 0) ||
      (expected_buffer_amount_ - actual_buffer_amount >
       kMinBufferedAmountDiffToTriggerCallback)) {
    uint64_t diff = expected_buffer_amount_ - actual_buffer_amount;
    expected_buffer_amount_ = actual_buffer_amount;
    observer_->OnBufferedAmountChange(diff);
  }

  // The threshold is always updated to ensure it's lower than what it's now.
  // This ensures that this function will be called again, until the channel is
  // completely drained.
  controller_->SetBufferedAmountLowThreshold(
      *id_n_,
      actual_buffer_amount > kMinBufferedAmountDiffToTriggerCallback
          ? actual_buffer_amount - kMinBufferedAmountDiffToTriggerCallback
          : 0);
}

// RTC_RUN_ON(network_thread_).
RTCError SctpDataChannel::SendDataMessage(const DataBuffer& buffer,
                                          bool queue_if_blocked) {
  SendDataParams send_params;
  if (!controller_ || !id_n_.has_value()) {
    error_ = RTCError(RTCErrorType::INVALID_STATE);
    return error_;
  }

  send_params.ordered = ordered_;
  // Send as ordered if it is still going through OPEN/ACK signaling.
  if (handshake_state_ != kHandshakeReady && !ordered_) {
    send_params.ordered = true;
    RTC_DLOG(LS_VERBOSE)
        << "Sending data as ordered for unordered DataChannel "
           "because the OPEN_ACK message has not been received.";
  }

  send_params.max_rtx_count = max_retransmits_;
  send_params.max_rtx_ms = max_retransmit_time_;
  send_params.type =
      buffer.binary ? DataMessageType::kBinary : DataMessageType::kText;

  error_ = controller_->SendData(*id_n_, send_params, buffer.data);
  MaybeSendOnBufferedAmountChanged();
  if (error_.ok()) {
    ++messages_sent_;
    bytes_sent_ += buffer.size();
    return error_;
  }

  // Close the channel if the error is not SDR_BLOCK, or if queuing the
  // message failed.
  RTC_LOG(LS_ERROR) << "Closing the DataChannel due to a failure to send data, "
                       "send_result = "
                    << ToString(error_.type()) << ":" << error_.message();
  // Return a copy of the error rather than `error_`, since the channel may get
  // deleted on the signaling thread as soon as it's closed (see `SetState()`).
  RTCError error(RTCErrorType::NETWORK_ERROR, "Failure to send data");
  CloseAbruptlyWithError(error);
  return error;
}

// RTC_RUN_ON(network_thread_).
bool SctpDataChannel::SendControlMessage(const CopyOnWriteBuffer& buffer) {
  RTC_DCHECK(connected_to_transport());
  RTC_DCHECK(id_n_.has_value());
  RTC_DCHECK(controller_);

  bool is_open_message = handshake_state_ == kHandshakeShouldSendOpen;
  RTC_DCHECK(!is_open_message || !negotiated_);

  SendDataParams send_params;
  // Send data as ordered before we receive any message from the remote peer to
  // make sure the remote peer will not receive any data before it receives the
  // OPEN message.
  send_params.ordered = ordered_ || is_open_message;
  send_params.type = DataMessageType::kControl;

  RTCError err = controller_->SendData(*id_n_, send_params, buffer);
  if (err.ok()) {
    RTC_DLOG(LS_VERBOSE) << "Sent CONTROL message on channel "
                         << id_n_->stream_id_int();

    if (handshake_state_ == kHandshakeShouldSendAck) {
      handshake_state_ = kHandshakeReady;
    } else if (handshake_state_ == kHandshakeShouldSendOpen) {
      handshake_state_ = kHandshakeWaitingForAck;
    }
  } else {
    RTC_LOG(LS_ERROR) << "Closing the DataChannel due to a failure to send"
                         " the CONTROL message, send_result = "
                      << ToString(err.type());
    err.set_message("Failed to send a CONTROL message");
    CloseAbruptlyWithError(err);
  }
  return err.ok();
}

// static
void SctpDataChannel::ResetInternalIdAllocatorForTesting(int new_value) {
  g_unique_id = new_value;
}

}  // namespace webrtc
