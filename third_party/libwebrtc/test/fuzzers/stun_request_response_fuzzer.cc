/*
 *  Copyright 2026 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Fuzzes STUN response handling: StunRequestManager::CheckResponse, the
// success/error branch split, and the re-entrancy contract between a response
// callback and the manager that dispatched it.
//
// Why the existing STUN fuzzers do not cover this:
//
//   stun_parser_fuzzer     parses a standalone StunMessage and validates its
//                          integrity against an empty password.
//   stun_validator_fuzzer  calls the static fingerprint/integrity validators on
//                          raw bytes.
//   turn_unwrap_fuzzer     calls UnwrapTurnPacket on raw bytes.
//
// All three are stateless: none constructs a StunRequestManager, so none can
// reach response handling at all. CheckResponse starts with
//
//     RequestMap::iterator iter = requests_.find(msg->transaction_id());
//     if (iter == requests_.end())
//       return false;
//
// so a fuzzer that never sent a request returns at the third line, every time.
//
// == Why the response is built rather than mutated ==
//
// CheckResponse sits behind three filters that a byte-level mutator cannot
// pass often enough to matter, so feeding it mutated bytes spends the budget in
// the parser instead of the state machine:
//
//   1. the transaction id must match a pending request  (96-bit needle)
//   2. the message type must equal GetStunSuccessResponseType(request_type) or
//      GetStunErrorResponseType(request_type)           (16-bit exact match)
//   3. on the success branch IntegrityOk() must hold, which requires a valid
//      HMAC-SHA1 over the message                       (not forgeable)
//
// Filter 3 makes the success branch unreachable by mutation in principle, not
// merely unlikely: OnResponse can never run. So this target builds the response
// with the real StunMessage builder and spends the fuzzer bytes on what is
// actually interesting -- which attributes are present, their lengths and
// contents, the error code, whether integrity is valid, and the order in which
// responses arrive relative to each other and to simulated time.
//
// Raw malformed wire input is already stun_parser_fuzzer's job; duplicating it
// here would only re-find what another target covers.
//
// == Why the callbacks do something ==
//
// StunRequest::OnResponse and OnErrorResponse are empty in the base class, so a
// target that sends a plain StunRequest reaches the dispatch and then executes
// nothing. The production handlers act on the manager from inside the callback,
// and CheckResponse explicitly permits that:
//
//     // Erase element from hash before calling callback. This ensures
//     // that the callback can modify the StunRequestManager any way it
//     // sees fit.
//
// Every action this target takes in a callback mirrors a real one, so a crash
// here corresponds to something production can do:
//
//   kSendAnotherRequest  TurnAllocateRequest::OnAuthChallenge (401) and
//                        OnTryAlternate (300) both send a fresh request from
//                        inside OnErrorResponse.
//   kClearManager        TurnPort tears its request set down on failure.
//   kDeferredClear       the ALLOCATION_MISMATCH (437) path, which webrtc
//                        defers with PostTask because, in its own words,
//                        "trying to delete the socket in OnErrorResponse will
//                        cause a deadlock on the socket".
//
// Re-entrancy is depth-capped: production bounds its retries (nonce attempts,
// allocate retries), and without a cap the only thing a fuzzer finds is its own
// unbounded recursion, which is not a memory-safety defect.

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/stun_request.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/span_helpers.h"
#include "test/create_test_environment.h"
#include "test/fuzzers/fuzz_data_helper.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {
namespace {

constexpr char kPassword[] = "fuzzpassword";
constexpr char kUsername[] = "fuzzuser";
constexpr char kRealm[] = "example.org";
constexpr char kNonce[] = "fuzznonce";

// Bounds, so that one input cannot turn into an unbounded run.
constexpr int kMaxReentrancyDepth = 4;
constexpr size_t kMaxPendingRequests = 4;
constexpr size_t kMaxAttributes = 8;
constexpr size_t kMaxAttributeBytes = 256;
constexpr size_t kMaxDeliveries = 16;

// What a response callback does before returning. See the mapping to the
// production handlers in the file comment.
enum class CallbackAction : uint8_t {
  kNothing = 0,
  kSendAnotherRequest,
  kClearManager,
  kDeferredClear,
};

// Keeping the menu next to the enum removes the artificial kCount sentinel.
// Two bits index it, which is what lets a re-entrant chain carry one action per
// level (see FuzzedRequest).
constexpr CallbackAction kCallbackActions[] = {
    CallbackAction::kNothing,
    CallbackAction::kSendAnotherRequest,
    CallbackAction::kClearManager,
    CallbackAction::kDeferredClear,
};

// Error codes production branches on, plus a few that land in its default
// path. Picking from a menu keeps those branches reachable; a uniformly random
// code would hit them almost never.
constexpr int kErrorCodes[] = {
    STUN_ERROR_TRY_ALTERNATE,        // 300 -> OnTryAlternate
    STUN_ERROR_UNAUTHORIZED,         // 401 -> OnAuthChallenge
    STUN_ERROR_ALLOCATION_MISMATCH,  // 437 -> deferred teardown
    438,                             // stale nonce
    400,
    420,
    441,
    486,
    500,
    508,
};

// Attribute types worth putting in a TURN/STUN response. The last entry is a
// comprehension-required type webrtc does not know, which drives the
// GetNonComprehendedAttributes discard path on purpose.
constexpr uint16_t kAttributeTypes[] = {
    STUN_ATTR_ERROR_CODE,
    STUN_ATTR_NONCE,
    STUN_ATTR_REALM,
    STUN_ATTR_UNKNOWN_ATTRIBUTES,
    STUN_ATTR_ALTERNATE_SERVER,
    STUN_ATTR_XOR_MAPPED_ADDRESS,
    STUN_ATTR_XOR_RELAYED_ADDRESS,
    STUN_ATTR_LIFETIME,
    STUN_ATTR_SOFTWARE,
    STUN_ATTR_DATA,
    0x7FFF,  // unknown, comprehension-required
};

// Mirrors a credentialed TURN Allocate retry: USERNAME/REALM/NONCE plus a
// MESSAGE-INTEGRITY over a real password, with AuthenticationRequired() left at
// its default. That combination is what makes CheckResponse take the
// integrity-checking path instead of the passwordless-test shortcut.
// TurnMessage rather than StunMessage, and not merely for realism: the
// comprehension check in CheckResponse goes through the virtual
// GetAttributeValueType, and CheckResponse parses the response with
// iter->second->msg_->CreateNew() -- that is, with the *request's* class. A
// plain StunMessage reports every TURN attribute (LIFETIME, DATA,
// XOR-RELAYED-ADDRESS, ...) as unknown, and since all of them sit in the
// comprehension-required range 0x0000-0x7FFF the whole response is discarded
// before the branch split. The request's class therefore decides how much of
// the attribute surface is reachable at all.
std::unique_ptr<StunMessage> BuildAuthenticatedRequest() {
  auto msg = std::make_unique<TurnMessage>(
      STUN_ALLOCATE_REQUEST, StunMessage::GenerateTransactionId());
  msg->AddAttribute(
      std::make_unique<StunByteStringAttribute>(STUN_ATTR_USERNAME, kUsername));
  msg->AddAttribute(
      std::make_unique<StunByteStringAttribute>(STUN_ATTR_REALM, kRealm));
  msg->AddAttribute(
      std::make_unique<StunByteStringAttribute>(STUN_ATTR_NONCE, kNonce));
  msg->AddMessageIntegrity(kPassword);
  msg->AddFingerprint();
  return msg;
}

class FuzzedRequest : public StunRequest {
 public:
  FuzzedRequest(const Environment& env,
                StunRequestManager& manager,
                std::unique_ptr<StunMessage> msg,
                uint8_t action_script,
                int depth)
      : StunRequest(env, manager, std::move(msg)),
        manager_(manager),
        action_script_(action_script),
        depth_(depth) {}

 private:
  void OnResponse(StunMessage* /* response */) override { Act(); }
  void OnErrorResponse(StunMessage* /* response */) override { Act(); }

  void Act() {
    // Two bits per level, so a re-entrant chain is not forced to repeat the
    // same action all the way down: the nested request below gets the
    // remaining bits and picks its own.
    switch (kCallbackActions[action_script_ & 0x3]) {
      case CallbackAction::kNothing:
        break;
      case CallbackAction::kSendAnotherRequest:
        if (depth_ < kMaxReentrancyDepth) {
          manager_.Send(std::make_unique<FuzzedRequest>(
              env(), manager_, BuildAuthenticatedRequest(),
              static_cast<uint8_t>(action_script_ >> 2), depth_ + 1));
        }
        break;
      case CallbackAction::kClearManager:
        manager_.Clear();
        break;
      case CallbackAction::kDeferredClear: {
        // The manager outlives every task posted here: FuzzOneInput drains the
        // queue inside the manager's scope, and Clear() posts nothing itself,
        // so the deferral is bounded at one hop.
        StunRequestManager* manager = &manager_;
        network_thread()->PostTask([manager] { manager->Clear(); });
        break;
      }
    }
  }

  StunRequestManager& manager_;
  const uint8_t action_script_;
  const int depth_;
};

// SocketAddress(uint32_t, int) is documented "V4 only, to be deprecated",
// and an ICE implementation has to cope with both families, so the address is
// fuzzed instead of being pinned to v4: one byte picks the family, then either
// 4 or 16 bytes of address. The byte order inside the address does not matter
// here -- what matters is that both families reach the attribute writers.
SocketAddress FuzzedAddress(FuzzDataHelper& fuzz_data) {
  const uint16_t port = fuzz_data.Read<uint16_t>();
  if (fuzz_data.Read<bool>()) {
    in6_addr v6 = {};
    std::span<const uint8_t> raw = fuzz_data.ReadByteArray(sizeof(v6.s6_addr));
    if (raw.size() == sizeof(v6.s6_addr)) {
      std::memcpy(v6.s6_addr, raw.data(), raw.size());
    }
    return SocketAddress(IPAddress(v6), port);
  }
  return SocketAddress(IPAddress(fuzz_data.Read<uint32_t>()), port);
}

// Adds one fuzzer-chosen attribute. Values come from the fuzz stream; the type
// comes from the menu above so the message stays structurally plausible.
void AddFuzzedAttribute(FuzzDataHelper& fuzz_data, StunMessage& msg) {
  const uint16_t type = fuzz_data.SelectOneOf(kAttributeTypes);

  switch (type) {
    case STUN_ATTR_ERROR_CODE: {
      const int code = fuzz_data.SelectOneOf(kErrorCodes);
      const size_t reason_len = fuzz_data.Read<uint8_t>() % 64;
      std::span<const uint8_t> raw = fuzz_data.ReadByteArray(reason_len);
      msg.AddAttribute(std::make_unique<StunErrorCodeAttribute>(
          type, code, std::string(AsStringView(raw))));
      break;
    }
    case STUN_ATTR_UNKNOWN_ATTRIBUTES: {
      // AddType grows the list and keeps the declared length in sync. SetType
      // assumes the list is already sized, and the (type, length) constructor
      // takes a length in bytes rather than a number of entries, so sizing it
      // from an entry count and then calling SetType walks off the end.
      const size_t count = fuzz_data.Read<uint8_t>() % 8;
      auto attr = std::make_unique<StunUInt16ListAttribute>(type, /*length=*/0);
      for (size_t i = 0; i < count; ++i) {
        attr->AddType(fuzz_data.Read<uint16_t>());
      }
      msg.AddAttribute(std::move(attr));
      break;
    }
    case STUN_ATTR_ALTERNATE_SERVER:
      msg.AddAttribute(std::make_unique<StunAddressAttribute>(
          type, FuzzedAddress(fuzz_data)));
      break;
    case STUN_ATTR_XOR_MAPPED_ADDRESS:
    case STUN_ATTR_XOR_RELAYED_ADDRESS:
      msg.AddAttribute(std::make_unique<StunXorAddressAttribute>(
          type, FuzzedAddress(fuzz_data)));
      break;
    case STUN_ATTR_LIFETIME:
      msg.AddAttribute(std::make_unique<StunUInt32Attribute>(
          type, fuzz_data.Read<uint32_t>()));
      break;
    default: {
      const size_t len = fuzz_data.Read<uint8_t>() % kMaxAttributeBytes;
      msg.AddAttribute(std::make_unique<StunByteStringAttribute>(
          type, fuzz_data.ReadByteArray(len)));
      break;
    }
  }
}

// Builds one response for `transaction_id`. The type selection deliberately
// includes a wrong type, so the "Received response with wrong type" path stays
// covered instead of becoming the only thing that ever happens.
Buffer BuildResponse(FuzzDataHelper& fuzz_data,
                     absl::string_view transaction_id) {
  int type = 0;
  switch (fuzz_data.Read<uint8_t>() % 3) {
    case 0:
      type = GetStunSuccessResponseType(STUN_ALLOCATE_REQUEST);
      break;
    case 1:
      type = GetStunErrorResponseType(STUN_ALLOCATE_REQUEST);
      break;
    default:
      type = GetStunSuccessResponseType(STUN_BINDING_REQUEST);
      break;
  }

  // TurnMessage for the same reason as the request; see the comment on
  // BuildAuthenticatedRequest.
  TurnMessage msg(static_cast<uint16_t>(type), transaction_id);

  const size_t num_attrs = fuzz_data.Read<uint8_t>() % (kMaxAttributes + 1);
  for (size_t i = 0; i < num_attrs && fuzz_data.BytesLeft() > 0; ++i) {
    AddFuzzedAttribute(fuzz_data, msg);
  }

  // Integrity decides whether the success branch runs at all, so it is a
  // first-class fuzzer decision rather than a constant.
  switch (fuzz_data.Read<uint8_t>() % 4) {
    case 0:
      msg.AddMessageIntegrity(kPassword);  // valid -> success branch reachable
      break;
    case 1:
      msg.AddMessageIntegrity("wrongpassword");
      break;
    case 2:
      msg.AddMessageIntegrity32(kPassword);
      break;
    default:
      break;  // absent
  }
  if (fuzz_data.Read<uint8_t>() & 1) {
    msg.AddFingerprint();
  }

  ByteBufferWriter writer;
  if (!msg.Write(&writer)) {
    return {};
  }
  return std::move(writer).Extract();
}

}  // namespace

void FuzzOneInput(FuzzDataHelper fuzz_data) {
  GlobalSimulatedTimeController time_controller(Timestamp::Seconds(1000));
  Environment env = CreateTestEnvironment({.time = &time_controller});

  {
    StunRequestManager manager(time_controller.GetMainThread(),
                               [](std::span<const uint8_t> /* packet */,
                                  StunRequest* /* request */) {});

    // Several requests in flight at once: with a single pending request the map
    // lookup is trivial and timeouts cannot interleave with dispatch.
    const size_t num_requests =
        1 + fuzz_data.Read<uint8_t>() % kMaxPendingRequests;
    std::vector<std::string> ids;
    ids.reserve(num_requests);
    for (size_t i = 0; i < num_requests; ++i) {
      std::unique_ptr<StunMessage> msg = BuildAuthenticatedRequest();
      ids.push_back(msg->transaction_id());
      // One byte carries up to four levels of actions, two bits each.
      const uint8_t action_script = fuzz_data.Read<uint8_t>();
      auto request = std::make_unique<FuzzedRequest>(
          env, manager, std::move(msg), action_script, /*depth=*/0);

      // The source documents two real configurations that clear this flag --
      // "STUN_BINDINGs from StunPort to a stun server" and "the initial
      // TURN_ALLOCATE_REQUEST" -- and they take a different branch of
      // CheckResponse entirely: unauthenticated success responses are
      // discarded outright, and unauthenticated error responses only pass for
      // code 401 or 300. Leaving the flag at its default keeps that whole
      // branch dark.
      //
      // The request keeps its MESSAGE-INTEGRITY either way. A request without
      // one sets skip_integrity_checking, which webrtc answers with
      // RTC_DCHECK(false) << "CheckResponse called on a passwordless request.
      // Fix test!" -- an assertion about harness misuse, not a defect, so it
      // is deliberately not reachable from here.
      if (fuzz_data.Read<bool>()) {
        request->SetAuthenticationRequired(false);
      }
      manager.Send(std::move(request));
    }

    for (size_t i = 0; i < kMaxDeliveries && fuzz_data.BytesLeft() > 4; ++i) {
      const uint8_t which = fuzz_data.Read<uint8_t>();

      // The high bit delivers a response for a transaction nobody waits on,
      // which is what a real endpoint sees on a stale or spoofed packet. The
      // rest target a pending request, out of order.
      const std::string id = (which & 0x80)
                                 ? StunMessage::GenerateTransactionId()
                                 : ids[(which & 0x7F) % ids.size()];

      Buffer packet = BuildResponse(fuzz_data, id);
      if (!packet.empty()) {
        const uint8_t mode = fuzz_data.Read<uint8_t>();
        if (mode & 1) {
          // The path TurnPort takes: it parses and validates the packet itself
          // and then hands the message object to the manager. This is the only
          // way to reach the branches of CheckResponse that handle an
          // already-validated message -- the matching-password shortcut and
          // RevalidateMessageIntegrity, which the source flags as a known
          // open issue (crbug.com/1177125) -- because a message parsed
          // inside CheckResponse(span) always arrives with integrity
          // kNotSet.
          ByteBufferReader reader(packet);
          TurnMessage parsed;
          if (parsed.Read(&reader)) {
            parsed.ValidateMessageIntegrity((mode & 2) ? kPassword
                                                       : "other-password");
            manager.CheckResponse(&parsed);
          }
        } else {
          manager.CheckResponse(packet);
        }
      }

      // Advancing simulated time fires retransmits and timeouts, so they
      // interleave with dispatch instead of only running at the end. The step
      // has to be able to reach seconds: retransmits start in the hundreds of
      // milliseconds and a request only times out after several seconds, so a
      // step capped at a few hundred milliseconds leaves OnTimeout unreachable.
      const uint8_t advance = fuzz_data.Read<uint8_t>();
      if (advance & 1) {
        time_controller.AdvanceTime(
            TimeDelta::Millis(1 + (advance >> 1) * 128));
      }
    }

    // Drain whatever a callback deferred, while the manager is still alive.
    manager.Clear();
    time_controller.AdvanceTime(TimeDelta::Zero());
  }

  // Let the timers settle once the manager is gone; nothing may reference it.
  time_controller.AdvanceTime(TimeDelta::Millis(1));
}

}  // namespace webrtc
