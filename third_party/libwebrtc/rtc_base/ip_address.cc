/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/ip_address.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "absl/strings/string_view.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/string_utils.h"

#if defined(WEBRTC_POSIX)
#ifdef OPENBSD
#include <netinet/in_systm.h>
#endif
#include <netdb.h>
#endif

namespace webrtc {

// Prefixes used for categorizing IPv6 addresses.
// V4 mapped addresses are defined in RFC 4291 section 2.5.5.
static const in6_addr kV4MappedPrefix = {
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0}}};
// 6to4 is defined in RFC 3056
static const in6_addr k6To4Prefix = {{{0x20, 0x02, 0}}};
// TEREDO is defined in RFC 8190.
static const in6_addr kTeredoPrefix = {{{0x20, 0x01, 0x00, 0x00}}};
// V4 compatible addresses are defined in RFC 4291 section 2.5.5.
static const in6_addr kV4CompatibilityPrefix = {{{0}}};
// 6bone is no longer in use, see RFC 5156
static const in6_addr k6BonePrefix = {{{0x3f, 0xfe, 0}}};
// Unique Local Address with the L bit set, defined in RFC 4193 section 3.1
static const in6_addr kPrivateNetworkPrefix = {{{0xFD}}};
// The NAT64 prefix is defined in RFC 6052 section 2.1
static const in6_addr kNat64Prefix = {
    {{0x00, 0x64, 0xff, 0x9b, 0, 0, 0, 0, 0, 0, 0, 0}}};

static bool IPIsHelper(const IPAddress& ip,
                       const in6_addr& tomatch,
                       int length);
static in_addr ExtractMappedAddress(const in6_addr& addr);

uint32_t IPAddress::v4AddressAsHostOrderInteger() const {
  if (family_ == AF_INET) {
    return NetworkToHost32(u_.ip4.s_addr);
  } else {
    return 0;
  }
}

int IPAddress::overhead() const {
  switch (family_) {
    case AF_INET:  // IPv4
      return 20;
    case AF_INET6:  // IPv6
      return 40;
    default:
      return 0;
  }
}

bool IPAddress::IsNil() const {
  return IPIsUnspec(*this);
}

size_t IPAddress::Size() const {
  switch (family_) {
    case AF_INET:
      return sizeof(in_addr);
    case AF_INET6:
      return sizeof(in6_addr);
  }
  return 0;
}

bool IPAddress::operator==(const IPAddress& other) const {
  if (family_ != other.family_) {
    return false;
  }
  if (family_ == AF_INET) {
    return memcmp(&u_.ip4, &other.u_.ip4, sizeof(u_.ip4)) == 0;
  }
  if (family_ == AF_INET6) {
    return memcmp(&u_.ip6, &other.u_.ip6, sizeof(u_.ip6)) == 0;
  }
  return family_ == AF_UNSPEC;
}

bool IPAddress::operator!=(const IPAddress& other) const {
  return !((*this) == other);
}

bool IPAddress::operator>(const IPAddress& other) const {
  return (*this) != other && !((*this) < other);
}

bool IPAddress::operator<(const IPAddress& other) const {
  // IPv4 is 'less than' IPv6
  if (family_ != other.family_) {
    if (family_ == AF_UNSPEC) {
      return true;
    }
    if (family_ == AF_INET && other.family_ == AF_INET6) {
      return true;
    }
    return false;
  }
  // Comparing addresses of the same family.
  switch (family_) {
    case AF_INET: {
      return NetworkToHost32(u_.ip4.s_addr) <
             NetworkToHost32(other.u_.ip4.s_addr);
    }
    case AF_INET6: {
      return memcmp(&u_.ip6.s6_addr, &other.u_.ip6.s6_addr, 16) < 0;
    }
  }
  // Catches AF_UNSPEC and invalid addresses.
  return false;
}

in6_addr IPAddress::ipv6_address() const {
  return u_.ip6;
}

in_addr IPAddress::ipv4_address() const {
  return u_.ip4;
}

std::string IPAddress::ToString() const {
  if (family_ != AF_INET && family_ != AF_INET6) {
    return std::string();
  }
  char buf[INET6_ADDRSTRLEN] = {0};
  const void* src = &u_.ip4;
  if (family_ == AF_INET6) {
    src = &u_.ip6;
  }
  if (!inet_ntop(family_, src, buf, sizeof(buf))) {
    return std::string();
  }
  return std::string(buf);
}

std::string IPAddress::ToSensitiveString() const {
  return std::string(Sensitive(*this).view());
}

IPAddress::Sensitive::Sensitive(const IPAddress& ip) {
  switch (ip.family()) {
    case AF_INET: {
      in_addr addr = ip.ipv4_address();
      if (!webrtc::inet_ntop(AF_INET, &addr, buffer_.data(), buffer_.size())) {
        return;
      }
      // Replace the last part with "x".
      size_t find_pos = absl::string_view(buffer_.data()).rfind('.');
      if (find_pos == absl::string_view::npos) {
        return;
      }
      buffer_[find_pos + 1] = 'x';
      length_ = find_pos + 2;
      return;
    }
    case AF_INET6: {
      in6_addr addr = ip.ipv6_address();
      int len = snprintf(buffer_.data(), buffer_.size(), "%x:%x:%x:x:x:x:x:x",
                         (addr.s6_addr[0] << 8) + addr.s6_addr[1],
                         (addr.s6_addr[2] << 8) + addr.s6_addr[3],
                         (addr.s6_addr[4] << 8) + addr.s6_addr[5]);
      if (len > 0) {
        length_ = static_cast<size_t>(len);
      }
      return;
    }
  }
}

absl::string_view IPAddress::Sensitive::view() const {
  return absl::string_view(buffer_.data(), length_);
}

IPAddress IPAddress::Normalized() const {
  if (family_ != AF_INET6) {
    return *this;
  }
  if (!IPIsV4Mapped(*this)) {
    return *this;
  }
  in_addr addr = ExtractMappedAddress(u_.ip6);
  return IPAddress(addr);
}

IPAddress IPAddress::NormalizeWithCheckForEmbeddedIPv4Address() const {
  if (family_ != AF_INET6) {
    return *this;
  }
  if (IPIsV4Mapped(*this) || IPIsV4Compatibility(*this) || IPIsNat64(*this)) {
    in_addr addr = ExtractMappedAddress(u_.ip6);
    return IPAddress(addr);
  }
  // TODO: crbug.com/497635018 - consider whether 6to4 addresses that map
  // to "local" IPv4 networks should be considered "local".
  // Enabling this mapping breaks downstream testing.
  // if (IPIs6To4(*this)) {
  //  in_addr addr;
  //  ::memcpy(&addr.s_addr, &u_.ip6.s6_addr[2], sizeof(addr.s_addr));
  //  return IPAddress(addr);
  //}
  if (IPIsTeredo(*this)) {
    in_addr addr;
    ::memcpy(&addr.s_addr, &u_.ip6.s6_addr[12], sizeof(addr.s_addr));
    addr.s_addr = ~addr.s_addr;
    return IPAddress(addr);
  }
  if (IPIsIsatap(*this)) {
    in_addr addr = ExtractMappedAddress(u_.ip6);
    return IPAddress(addr);
  }
  return *this;
}

IPAddress IPAddress::AsIPv6Address() const {
  if (family_ != AF_INET) {
    return *this;
  }
  in6_addr v6addr = kV4MappedPrefix;
  ::memcpy(&v6addr.s6_addr[12], &u_.ip4.s_addr, sizeof(u_.ip4.s_addr));
  return IPAddress(v6addr);
}

bool InterfaceAddress::operator==(const InterfaceAddress& other) const {
  return ipv6_flags_ == other.ipv6_flags() &&
         static_cast<const IPAddress&>(*this) == other;
}

bool InterfaceAddress::operator!=(const InterfaceAddress& other) const {
  return !((*this) == other);
}

const InterfaceAddress& InterfaceAddress::operator=(
    const InterfaceAddress& other) {
  ipv6_flags_ = other.ipv6_flags_;
  static_cast<IPAddress&>(*this) = other;
  return *this;
}

std::string InterfaceAddress::ToString() const {
  std::string result = IPAddress::ToString();

  if (family() == AF_INET6)
    result += "|flags:0x" + ToHex(ipv6_flags());

  return result;
}

static bool IPIsPrivateNetworkV4(const IPAddress& ip) {
  uint32_t ip_in_host_order = ip.v4AddressAsHostOrderInteger();
  return ((ip_in_host_order >> 24) == 10) ||
         ((ip_in_host_order >> 20) == ((172 << 4) | 1)) ||
         ((ip_in_host_order >> 16) == ((192 << 8) | 168));
}

static bool IPIsPrivateNetworkV6(const IPAddress& ip) {
  return IPIsHelper(ip, kPrivateNetworkPrefix, 8);
}

bool IPIsPrivateNetwork(const IPAddress& ip) {
  IPAddress normalized = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  switch (normalized.family()) {
    case AF_INET: {
      return IPIsPrivateNetworkV4(normalized);
    }
    case AF_INET6: {
      return IPIsPrivateNetworkV6(normalized);
    }
  }
  return false;
}

static bool IPIsSharedNetworkV4(const IPAddress& ip) {
  uint32_t ip_in_host_order = ip.v4AddressAsHostOrderInteger();
  return (ip_in_host_order >> 22) == ((100 << 2) | 1);
}

bool IPIsSharedNetwork(const IPAddress& ip) {
  IPAddress normalized = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  if (normalized.family() == AF_INET) {
    return IPIsSharedNetworkV4(normalized);
  }
  return false;
}

in_addr ExtractMappedAddress(const in6_addr& in6) {
  in_addr ipv4;
  ::memcpy(&ipv4.s_addr, &in6.s6_addr[12], sizeof(ipv4.s_addr));
  return ipv4;
}

bool IPFromAddrInfo(struct addrinfo* info, IPAddress* out) {
  if (!info || !info->ai_addr) {
    return false;
  }
  if (info->ai_addr->sa_family == AF_INET) {
    sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(info->ai_addr);
    *out = IPAddress(addr->sin_addr);
    return true;
  } else if (info->ai_addr->sa_family == AF_INET6) {
    sockaddr_in6* addr = reinterpret_cast<sockaddr_in6*>(info->ai_addr);
    *out = IPAddress(addr->sin6_addr);
    return true;
  }
  return false;
}

bool IPFromString(absl::string_view str, IPAddress* out) {
  if (!out) {
    return false;
  }
  in_addr addr;
  if (inet_pton(AF_INET, str, &addr) == 0) {
    in6_addr addr6;
    if (inet_pton(AF_INET6, str, &addr6) == 0) {
      *out = IPAddress();
      return false;
    }
    *out = IPAddress(addr6);
  } else {
    *out = IPAddress(addr);
  }
  return true;
}

bool IPFromString(absl::string_view str, int flags, InterfaceAddress* out) {
  IPAddress ip;
  if (!IPFromString(str, &ip)) {
    return false;
  }

  *out = InterfaceAddress(ip, flags);
  return true;
}

bool IPIsAny(const IPAddress& ip) {
  switch (ip.family()) {
    case AF_INET:
      return ip == IPAddress(INADDR_ANY);
    case AF_INET6:
      return ip == IPAddress(in6addr_any) || ip == IPAddress(kV4MappedPrefix);
    case AF_UNSPEC:
      return false;
  }
  return false;
}

static bool IPIsLoopbackV4(const IPAddress& ip) {
  uint32_t ip_in_host_order = ip.v4AddressAsHostOrderInteger();
  return ((ip_in_host_order >> 24) == 127);
}

static bool IPIsLoopbackV6(const IPAddress& ip) {
  return ip == IPAddress(in6addr_loopback);
}

bool IPIsLoopback(const IPAddress& ip) {
  IPAddress normalized = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  switch (normalized.family()) {
    case AF_INET: {
      return IPIsLoopbackV4(normalized);
    }
    case AF_INET6: {
      return IPIsLoopbackV6(normalized);
    }
  }
  return false;
}

bool IPIsPrivate(const IPAddress& ip) {
  return IPIsLinkLocal(ip) || IPIsLoopback(ip) || IPIsPrivateNetwork(ip) ||
         IPIsSharedNetwork(ip);
}

bool IPIsUnspec(const IPAddress& ip) {
  return ip.family() == AF_UNSPEC;
}

size_t HashIP(const IPAddress& ip) {
  IPAddress normalized = ip.Normalized();
  switch (normalized.family()) {
    case AF_INET: {
      return normalized.ipv4_address().s_addr;
    }
    case AF_INET6: {
      in6_addr v6addr = normalized.ipv6_address();
      const uint32_t* v6_as_ints =
          reinterpret_cast<const uint32_t*>(&v6addr.s6_addr);
      return v6_as_ints[0] ^ v6_as_ints[1] ^ v6_as_ints[2] ^ v6_as_ints[3];
    }
  }
  return 0;
}

IPAddress TruncateIP(const IPAddress& ip, int length) {
  if (length < 0) {
    return IPAddress();
  }
  if (ip.family() == AF_INET) {
    if (length > 31) {
      return ip;
    }
    if (length == 0) {
      return IPAddress(INADDR_ANY);
    }
    int mask = (0xFFFFFFFF << (32 - length));
    uint32_t host_order_ip = NetworkToHost32(ip.ipv4_address().s_addr);
    in_addr masked;
    masked.s_addr = HostToNetwork32(host_order_ip & mask);
    return IPAddress(masked);
  } else if (ip.family() == AF_INET6) {
    if (length > 127) {
      return ip;
    }
    if (length == 0) {
      return IPAddress(in6addr_any);
    }
    in6_addr v6addr = ip.ipv6_address();
    int position = length / 32;
    int inner_length = 32 - (length - (position * 32));
    // Note: 64bit mask constant needed to allow possible 32-bit left shift.
    uint32_t inner_mask = 0xFFFFFFFFLL << inner_length;
    uint32_t* v6_as_ints = reinterpret_cast<uint32_t*>(&v6addr.s6_addr);
    for (int i = 0; i < 4; ++i) {
      if (i == position) {
        uint32_t host_order_inner = NetworkToHost32(v6_as_ints[i]);
        v6_as_ints[i] = HostToNetwork32(host_order_inner & inner_mask);
      } else if (i > position) {
        v6_as_ints[i] = 0;
      }
    }
    return IPAddress(v6addr);
  }
  return IPAddress();
}

int CountIPMaskBits(const IPAddress& mask) {
  uint32_t word_to_count = 0;
  int bits = 0;
  switch (mask.family()) {
    case AF_INET: {
      word_to_count = NetworkToHost32(mask.ipv4_address().s_addr);
      break;
    }
    case AF_INET6: {
      in6_addr v6addr = mask.ipv6_address();
      const uint32_t* v6_as_ints =
          reinterpret_cast<const uint32_t*>(&v6addr.s6_addr);
      int i = 0;
      for (; i < 4; ++i) {
        if (v6_as_ints[i] != 0xFFFFFFFF) {
          break;
        }
      }
      if (i < 4) {
        word_to_count = NetworkToHost32(v6_as_ints[i]);
      }
      bits = (i * 32);
      break;
    }
    default: {
      return 0;
    }
  }
  if (word_to_count == 0) {
    return bits;
  }

  // Public domain bit-twiddling hack from:
  // http://graphics.stanford.edu/~seander/bithacks.html
  // Counts the trailing 0s in the word.
  unsigned int zeroes = 32;
  // This could also be written word_to_count &= -word_to_count, but
  // MSVC emits warning C4146 when negating an unsigned number.
  word_to_count &= ~word_to_count + 1;  // Isolate lowest set bit.
  if (word_to_count)
    zeroes--;
  if (word_to_count & 0x0000FFFF)
    zeroes -= 16;
  if (word_to_count & 0x00FF00FF)
    zeroes -= 8;
  if (word_to_count & 0x0F0F0F0F)
    zeroes -= 4;
  if (word_to_count & 0x33333333)
    zeroes -= 2;
  if (word_to_count & 0x55555555)
    zeroes -= 1;

  return bits + (32 - zeroes);
}

bool IPIsHelper(const IPAddress& ip, const in6_addr& tomatch, int length) {
  // Helper method for checking IP prefix matches (but only on whole byte
  // lengths). Length is in bits.
  in6_addr addr = ip.ipv6_address();
  return ::memcmp(&addr, &tomatch, (length >> 3)) == 0;
}

bool IPIs6Bone(const IPAddress& ip) {
  return ip.family() == AF_INET6 && IPIsHelper(ip, k6BonePrefix, 16);
}

bool IPIs6To4(const IPAddress& ip) {
  return ip.family() == AF_INET6 && IPIsHelper(ip, k6To4Prefix, 16);
}

bool IPIsIsatap(const IPAddress& ip) {
  if (ip.family() != AF_INET6) {
    return false;
  }
  in6_addr addr = ip.ipv6_address();
  return (addr.s6_addr[8] == 0x00 || addr.s6_addr[8] == 0x02) &&
         addr.s6_addr[9] == 0x00 && addr.s6_addr[10] == 0x5E &&
         addr.s6_addr[11] == 0xFE;
}

static bool IPIsLinkLocalV4(const IPAddress& ip) {
  uint32_t ip_in_host_order = ip.v4AddressAsHostOrderInteger();
  return ((ip_in_host_order >> 16) == ((169 << 8) | 254));
}

static bool IPIsLinkLocalV6(const IPAddress& ip) {
  // Can't use the helper because the prefix is 10 bits.
  in6_addr addr = ip.ipv6_address();
  return (addr.s6_addr[0] == 0xFE) && ((addr.s6_addr[1] & 0xC0) == 0x80);
}

bool IPIsLinkLocal(const IPAddress& ip) {
  IPAddress normalized = ip.NormalizeWithCheckForEmbeddedIPv4Address();
  switch (normalized.family()) {
    case AF_INET: {
      return IPIsLinkLocalV4(normalized);
    }
    case AF_INET6: {
      return IPIsLinkLocalV6(normalized);
    }
  }
  return false;
}

// According to http://www.ietf.org/rfc/rfc2373.txt, Appendix A, page 19.  An
// address which contains MAC will have its 11th and 12th bytes as FF:FE as well
// as the U/L bit as 1.
bool IPIsMacBased(const IPAddress& ip) {
  in6_addr addr = ip.ipv6_address();
  return ((addr.s6_addr[8] & 0x02) && addr.s6_addr[11] == 0xFF &&
          addr.s6_addr[12] == 0xFE);
}

bool IPIsNat64(const IPAddress& ip) {
  return ip.family() == AF_INET6 && IPIsHelper(ip, kNat64Prefix, 96);
}

bool IPIsSiteLocal(const IPAddress& ip) {
  // Can't use the helper because the prefix is 10 bits.
  if (ip.family() != AF_INET6) {
    return false;
  }
  in6_addr addr = ip.ipv6_address();
  return addr.s6_addr[0] == 0xFE && (addr.s6_addr[1] & 0xC0) == 0xC0;
}

bool IPIsULA(const IPAddress& ip) {
  // Can't use the helper because the prefix is 7 bits.
  if (ip.family() != AF_INET6) {
    return false;
  }
  in6_addr addr = ip.ipv6_address();
  return (addr.s6_addr[0] & 0xFE) == 0xFC;
}

bool IPIsTeredo(const IPAddress& ip) {
  return ip.family() == AF_INET6 && IPIsHelper(ip, kTeredoPrefix, 32);
}

bool IPIsV4Compatibility(const IPAddress& ip) {
  if (ip.family() != AF_INET6 || !IPIsHelper(ip, kV4CompatibilityPrefix, 96)) {
    return false;
  }
  in_addr v4 = ExtractMappedAddress(ip.ipv6_address());
  uint32_t v4_h = NetworkToHost32(v4.s_addr);
  return v4_h != 0 && v4_h != 1;
}

bool IPIsV4Mapped(const IPAddress& ip) {
  return ip.family() == AF_INET6 && IPIsHelper(ip, kV4MappedPrefix, 96);
}

int IPAddressPrecedence(const IPAddress& ip) {
  // Precedence values from RFC 3484-bis. Prefers native v4 over 6to4/Teredo.
  if (ip.family() == AF_INET) {
    return 30;
  } else if (ip.family() == AF_INET6) {
    if (IPIsLoopback(ip)) {
      return 60;
    } else if (IPIsULA(ip)) {
      return 50;
    } else if (IPIsV4Mapped(ip)) {
      return 30;
    } else if (IPIs6To4(ip)) {
      return 20;
    } else if (IPIsTeredo(ip)) {
      return 10;
    } else if (IPIsV4Compatibility(ip) || IPIsSiteLocal(ip) || IPIs6Bone(ip)) {
      return 1;
    } else {
      // A 'normal' IPv6 address.
      return 40;
    }
  }
  return 0;
}

IPAddress GetLoopbackIP(int family) {
  if (family == AF_INET) {
    return IPAddress(INADDR_LOOPBACK);
  }
  if (family == AF_INET6) {
    return IPAddress(in6addr_loopback);
  }
  return IPAddress();
}

IPAddress GetAnyIP(int family) {
  if (family == AF_INET) {
    return IPAddress(INADDR_ANY);
  }
  if (family == AF_INET6) {
    return IPAddress(in6addr_any);
  }
  return IPAddress();
}

}  // namespace webrtc
