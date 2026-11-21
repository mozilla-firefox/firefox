/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/candidate.h"

#include "absl/base/attributes.h"
#include "p2p/base/p2p_constants.h"
#include "rtc_base/crc32.h"
#include "rtc_base/helpers.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"

using webrtc::IceCandidateType;

namespace webrtc {
absl::string_view IceCandidateTypeToString(IceCandidateType type) {
  switch (type) {
    case IceCandidateType::kHost:
      return "host";
    case IceCandidateType::kSrflx:
      return "srflx";
    case IceCandidateType::kPrflx:
      return "prflx";
    case IceCandidateType::kRelay:
      return "relay";
  }
}
}  // namespace webrtc

namespace cricket {

Candidate::Candidate()
    : id_(rtc::CreateRandomString(8)),
      component_(ICE_CANDIDATE_COMPONENT_DEFAULT),
      priority_(0),
      network_type_(rtc::ADAPTER_TYPE_UNKNOWN),
      underlying_type_for_vpn_(rtc::ADAPTER_TYPE_UNKNOWN),
      generation_(0),
      network_id_(0),
      network_cost_(0) {}

Candidate::Candidate(int component,
                     absl::string_view protocol,
                     const rtc::SocketAddress& address,
                     uint32_t priority,
                     absl::string_view username,
                     absl::string_view password,
                     webrtc::IceCandidateType type,
                     uint32_t generation,
                     absl::string_view foundation,
                     uint16_t network_id /*= 0*/,
                     uint16_t network_cost /*= 0*/)
    : id_(rtc::CreateRandomString(8)),
      component_(component),
      protocol_(protocol),
      address_(address),
      priority_(priority),
      username_(username),
      password_(password),
      type_(type),
      network_type_(rtc::ADAPTER_TYPE_UNKNOWN),
      underlying_type_for_vpn_(rtc::ADAPTER_TYPE_UNKNOWN),
      generation_(generation),
      foundation_(foundation),
      network_id_(network_id),
      network_cost_(network_cost) {}

Candidate::Candidate(const Candidate&) = default;

Candidate::~Candidate() = default;

void Candidate::generate_id() {
  id_ = rtc::CreateRandomString(8);
}

bool Candidate::is_local() const {
  return type_ == IceCandidateType::kHost;
}
bool Candidate::is_stun() const {
  return type_ == IceCandidateType::kSrflx;
}
bool Candidate::is_prflx() const {
  return type_ == IceCandidateType::kPrflx;
}
bool Candidate::is_relay() const {
  return type_ == IceCandidateType::kRelay;
}

absl::string_view Candidate::type_name() const {
  return webrtc::IceCandidateTypeToString(type_);
}

bool Candidate::IsEquivalent(const Candidate& c) const {
  // We ignore the network name, since that is just debug information, and
  // the priority and the network cost, since they should be the same if the
  // rest are.
  return (component_ == c.component_) && (protocol_ == c.protocol_) &&
         (address_ == c.address_) && (username_ == c.username_) &&
         (password_ == c.password_) && (type_ == c.type_) &&
         (generation_ == c.generation_) && (foundation_ == c.foundation_) &&
         (related_address_ == c.related_address_) &&
         (network_id_ == c.network_id_);
}

bool Candidate::MatchesForRemoval(const Candidate& c) const {
  return component_ == c.component_ && protocol_ == c.protocol_ &&
         address_ == c.address_;
}

std::string Candidate::ToStringInternal(bool sensitive) const {
  rtc::StringBuilder ost;
  std::string address =
      sensitive ? address_.ToSensitiveString() : address_.ToString();
  std::string related_address = sensitive ? related_address_.ToSensitiveString()
                                          : related_address_.ToString();
  ost << "Cand[" << transport_name_ << ":" << foundation_ << ":" << component_
      << ":" << protocol_ << ":" << priority_ << ":" << address << ":"
      << type_name() << ":" << related_address << ":" << username_ << ":"
      << password_ << ":" << network_id_ << ":" << network_cost_ << ":"
      << generation_ << "]";
  return ost.Release();
}

uint32_t Candidate::GetPriority(uint32_t type_preference,
                                int network_adapter_preference,
                                int relay_preference,
                                bool adjust_local_preference) const {
  // RFC 5245 - 4.1.2.1.
  // priority = (2^24)*(type preference) +
  //            (2^8)*(local preference) +
  //            (2^0)*(256 - component ID)

  // `local_preference` length is 2 bytes, 0-65535 inclusive.
  // In our implemenation we will partion local_preference into
  //              0                 1
  //       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
  //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //      |  NIC Pref     |    Addr Pref  |
  //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // NIC Type - Type of the network adapter e.g. 3G/Wifi/Wired.
  // Addr Pref - Address preference value as per RFC 3484.
  // local preference =  (NIC Type << 8 | Addr_Pref) + relay preference.
  // The relay preference is based on the number of TURN servers, the
  // first TURN server gets the highest preference.
  int addr_pref = IPAddressPrecedence(address_.ipaddr());
  int local_preference =
      ((network_adapter_preference << 8) | addr_pref) + relay_preference;

  // Ensure that the added relay preference will not result in a relay candidate
  // whose STUN priority attribute has a higher priority than a server-reflexive
  // candidate.
  // The STUN priority attribute is calculated as
  // (peer-reflexive type preference) << 24 | (priority & 0x00FFFFFF)
  // as described in
  // https://www.rfc-editor.org/rfc/rfc5245#section-7.1.2.1
  // To satisfy that condition, add kMaxTurnServers to the local preference.
  // This can not overflow the field width since the highest "NIC pref"
  // assigned is kHighestNetworkPreference = 127
  RTC_DCHECK_LT(local_preference + kMaxTurnServers, 0x10000);
  if (adjust_local_preference && relay_protocol_.empty()) {
    local_preference += kMaxTurnServers;
  }

  return (type_preference << 24) | (local_preference << 8) | (256 - component_);
}

bool Candidate::operator==(const Candidate& o) const {
  return id_ == o.id_ && component_ == o.component_ &&
         protocol_ == o.protocol_ && relay_protocol_ == o.relay_protocol_ &&
         address_ == o.address_ && priority_ == o.priority_ &&
         username_ == o.username_ && password_ == o.password_ &&
         type_ == o.type_ && network_name_ == o.network_name_ &&
         network_type_ == o.network_type_ && generation_ == o.generation_ &&
         foundation_ == o.foundation_ &&
         related_address_ == o.related_address_ && tcptype_ == o.tcptype_ &&
         transport_name_ == o.transport_name_ && network_id_ == o.network_id_;
}

bool Candidate::operator!=(const Candidate& o) const {
  return !(*this == o);
}

Candidate Candidate::ToSanitizedCopy(bool use_hostname_address,
                                     bool filter_related_address) const {
  Candidate copy(*this);
  if (use_hostname_address) {
    rtc::IPAddress ip;
    if (address().hostname().empty()) {
      // IP needs to be redacted, but no hostname available.
      rtc::SocketAddress redacted_addr("redacted-ip.invalid", address().port());
      copy.set_address(redacted_addr);
    } else if (IPFromString(address().hostname(), &ip)) {
      // The hostname is an IP literal, and needs to be redacted too.
      rtc::SocketAddress redacted_addr("redacted-literal.invalid",
                                       address().port());
      copy.set_address(redacted_addr);
    } else {
      rtc::SocketAddress hostname_only_addr(address().hostname(),
                                            address().port());
      copy.set_address(hostname_only_addr);
    }
  }
  if (filter_related_address) {
    copy.set_related_address(
        rtc::EmptySocketAddressWithFamily(copy.address().family()));
  }
  return copy;
}

void Candidate::ComputeFoundation(const rtc::SocketAddress& base_address,
                                  uint64_t tie_breaker) {
  // https://www.rfc-editor.org/rfc/rfc5245#section-4.1.1.3
  // The foundation is an identifier, scoped within a session.  Two candidates
  // MUST have the same foundation ID when all of the following are true:
  //
  // o they are of the same type.
  // o their bases have the same IP address (the ports can be different).
  // o for reflexive and relayed candidates, the STUN or TURN servers used to
  //   obtain them have the same IP address.
  // o they were obtained using the same transport protocol (TCP, UDP, etc.).
  //
  // Similarly, two candidates MUST have different foundations if their
  // types are different, their bases have different IP addresses, the STUN or
  // TURN servers used to obtain them have different IP addresses, or their
  // transport protocols are different.

  rtc::StringBuilder sb;
  sb << type_name() << base_address.ipaddr().ToString() << protocol_
     << relay_protocol_;

  // https://www.rfc-editor.org/rfc/rfc5245#section-5.2
  // [...] it is possible for both agents to mistakenly believe they are
  // controlled or controlling. To resolve this, each agent MUST select a random
  // number, called the tie-breaker, uniformly distributed between 0 and (2**64)
  // - 1 (that is, a 64-bit positive integer).  This number is used in
  // connectivity checks to detect and repair this case [...]
  sb << rtc::ToString(tie_breaker);
  foundation_ = rtc::ToString(rtc::ComputeCrc32(sb.Release()));
}

void Candidate::ComputePrflxFoundation() {
  RTC_DCHECK(is_prflx());
  RTC_DCHECK(!id_.empty());
  foundation_ = rtc::ToString(rtc::ComputeCrc32(id_));
}

void Candidate::Assign(std::string& s, absl::string_view view) {
  // Assigning via a temporary object, like s = std::string(view), results in
  // binary size bloat. To avoid that, extract pointer and size from the
  // string view, and use std::string::assign method.
  s.assign(view.data(), view.size());
}

}  // namespace cricket
