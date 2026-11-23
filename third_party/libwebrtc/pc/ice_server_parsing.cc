/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/ice_server_parsing.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "rtc_base/checks.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/string_to_number.h"

namespace webrtc {

namespace {
// Number of tokens must be preset when TURN uri has transport param.
const size_t kTurnTransportTokensNum = 2;
// The default stun port.
const int kDefaultStunPort = 3478;
const int kDefaultStunTlsPort = 5349;
const char kTransport[] = "transport";

// Allowed characters in hostname per RFC 3986 Appendix A "reg-name"
const char kRegNameCharacters[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "-._~"          // unreserved
    "%"             // pct-encoded
    "!$&'()*+,;=";  // sub-delims

// NOTE: Must be in the same order as the ServiceType enum.
constexpr std::array<absl::string_view, 4> kValidIceServiceTypes{
    "stun", "stuns", "turn", "turns"};

// NOTE: A loop below assumes that the first value of this enum is 0 and all
// other values are incremental.
enum class ServiceType {
  STUN = 0,  // Indicates a STUN server.
  STUNS,     // Indicates a STUN server used with a TLS session.
  TURN,      // Indicates a TURN server
  TURNS,     // Indicates a TURN server used with a TLS session.
  INVALID,   // Unknown.
};
static_assert(static_cast<size_t>(ServiceType::INVALID) ==
                  std::size(kValidIceServiceTypes),
              "kValidIceServiceTypes must have as many strings as ServiceType "
              "has values.");

// `in_str` should follow of RFC 7064/7065 syntax, but with an optional
// "?transport=" already stripped. I.e.,
// stunURI       = scheme ":" host [ ":" port ]
// scheme        = "stun" / "stuns" / "turn" / "turns"
// host          = IP-literal / IPv4address / reg-name
// port          = *DIGIT

// Return tuple is service_type, host, with service_type == ServiceType::INVALID
// on failure.
std::tuple<ServiceType, absl::string_view> GetServiceTypeAndHostnameFromUri(
    absl::string_view in_str) {
  const auto colonpos = in_str.find(':');
  if (colonpos == absl::string_view::npos) {
    RTC_LOG(LS_WARNING) << "Missing ':' in ICE URI: " << in_str;
    return {ServiceType::INVALID, ""};
  }
  if ((colonpos + 1) == in_str.length()) {
    RTC_LOG(LS_WARNING) << "Empty hostname in ICE URI: " << in_str;
    return {ServiceType::INVALID, ""};
  }
  for (size_t i = 0; i < std::size(kValidIceServiceTypes); ++i) {
    if (in_str.compare(0, colonpos, kValidIceServiceTypes[i]) == 0) {
      return {static_cast<ServiceType>(i), in_str.substr(colonpos + 1)};
    }
  }
  return {ServiceType::INVALID, ""};
}

std::optional<int> ParsePort(absl::string_view in_str) {
  // Make sure port only contains digits. StringToNumber doesn't check this.
  for (const char& c : in_str) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return StringToNumber<int>(in_str);
}

// This method parses IPv6 and IPv4 literal strings, along with hostnames in
// standard hostname:port format.
// Consider following formats as correct.
// `hostname:port`, |[IPV6 address]:port|, |IPv4 address|:port,
// `hostname`, |[IPv6 address]|, |IPv4 address|.

// Return tuple is success, host, port.
std::tuple<bool, absl::string_view, int> ParseHostnameAndPortFromString(
    absl::string_view in_str,
    int default_port) {
  if (in_str.empty()) {
    return {false, "", 0};
  }
  absl::string_view host;
  int port = default_port;

  if (in_str.at(0) == '[') {
    // IP_literal syntax
    auto closebracket = in_str.rfind(']');
    if (closebracket == absl::string_view::npos) {
      return {false, "", 0};
    }
    auto colonpos = in_str.find(':', closebracket);
    if (absl::string_view::npos != colonpos) {
      if (std::optional<int> opt_port =
              ParsePort(in_str.substr(closebracket + 2))) {
        port = *opt_port;
      } else {
        return {false, "", 0};
      }
    }
    host = in_str.substr(1, closebracket - 1);
  } else {
    // IPv4address or reg-name syntax
    auto colonpos = in_str.find(':');
    if (absl::string_view::npos != colonpos) {
      if (std::optional<int> opt_port =
              ParsePort(in_str.substr(colonpos + 1))) {
        port = *opt_port;
      } else {
        return {false, "", 0};
      }
      host = in_str.substr(0, colonpos);
    } else {
      host = in_str;
    }
    // RFC 3986 section 3.2.2 and Appendix A - "reg-name" syntax
    if (host.find_first_not_of(kRegNameCharacters) != absl::string_view::npos) {
      return {false, "", 0};
    }
  }
  return {!host.empty(), host, port};
}

// Adds a STUN or TURN server to the appropriate list,
// by parsing `url` and using the username/password in `server`.
RTCError ParseIceServerUrl(const PeerConnectionInterface::IceServer& server,
                           absl::string_view url,
                           ServerAddresses* stun_servers,
                           std::vector<RelayServerConfig>* turn_servers,
                           std::optional<size_t> max_stun_servers,
                           ServerAddresses& ignored_stun_servers) {
  // RFC 7064
  // stunURI       = scheme ":" host [ ":" port ]
  // scheme        = "stun" / "stuns"

  // RFC 7065
  // turnURI       = scheme ":" host [ ":" port ]
  //                 [ "?transport=" transport ]
  // scheme        = "turn" / "turns"
  // transport     = "udp" / "tcp" / transport-ext
  // transport-ext = 1*unreserved

  // RFC 3986
  // host     = IP-literal / IPv4address / reg-name
  // port     = *DIGIT

  RTC_DCHECK(stun_servers != nullptr);
  RTC_DCHECK(turn_servers != nullptr);
  ProtocolType turn_transport_type = PROTO_UDP;
  RTC_DCHECK(!url.empty());
  std::vector<absl::string_view> tokens = split(url, '?');
  absl::string_view uri_without_transport = tokens[0];
  // Let's look into transport= param, if it exists.
  bool transport_given_explicitly = false;
  if (tokens.size() == kTurnTransportTokensNum) {  // ?transport= is present.
    std::vector<absl::string_view> transport_tokens = split(tokens[1], '=');
    if (transport_tokens[0] != kTransport) {
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::SYNTAX_ERROR)
          << "ICE server parsing failed: Invalid transport parameter key.");
    }
    if (transport_tokens.size() < 2) {
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::SYNTAX_ERROR)
          << "ICE server parsing failed: Transport parameter missing value.");
    }

    std::optional<ProtocolType> proto = StringToProto(transport_tokens[1]);
    if (!proto || (*proto != PROTO_UDP && *proto != PROTO_TCP)) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::SYNTAX_ERROR)
                           << "ICE server parsing failed: Transport parameter "
                              "should always be udp or tcp.");
    }
    turn_transport_type = *proto;
    transport_given_explicitly = true;
  }

  auto [service_type, hoststring] =
      GetServiceTypeAndHostnameFromUri(uri_without_transport);
  if (service_type == ServiceType::INVALID) {
    RTC_LOG(LS_ERROR) << "Invalid transport parameter in ICE URI: " << url;
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::SYNTAX_ERROR)
        << "ICE server parsing failed: Invalid transport parameter in ICE URI");
  }

  // GetServiceTypeAndHostnameFromUri should never give an empty hoststring
  RTC_DCHECK(!hoststring.empty());

  // stun with ?transport (or any ?) is not valid.
  if ((service_type == ServiceType::STUN ||
       service_type == ServiceType::STUNS) &&
      tokens.size() > 1) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::SYNTAX_ERROR)
        << "ICE server parsing failed: Invalid stun url with query parameters");
  }

  int default_port = kDefaultStunPort;
  if (service_type == ServiceType::TURNS) {
    default_port = kDefaultStunTlsPort;
    turn_transport_type = PROTO_TLS;
    // When transport is given explicitly, the default transport is not
    // specified in the RFC, but it's long-standing behavior to use TLS.
    if (!transport_given_explicitly || turn_transport_type == PROTO_TCP) {
      turn_transport_type = PROTO_TLS;
    } else if (turn_transport_type == PROTO_UDP) {
      turn_transport_type = PROTO_DTLS;
    }
  }

  if (hoststring.find('@') != absl::string_view::npos) {
    RTC_LOG(LS_ERROR) << "Invalid url with long deprecated user@host syntax: "
                      << uri_without_transport;
    return RTC_LOG_ERROR(RTCError(RTCErrorType::SYNTAX_ERROR)
                         << "ICE server parsing failed: Invalid url with long "
                            "deprecated user@host syntax");
  }

  auto [success, address, port] =
      ParseHostnameAndPortFromString(hoststring, default_port);
  if (!success) {
    RTC_LOG(LS_ERROR) << "Invalid hostname format: " << uri_without_transport;
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::SYNTAX_ERROR)
        << "ICE server parsing failed: Invalid hostname format");
  }

  if (port <= 0 || port > 0xffff) {
    RTC_LOG(LS_ERROR) << "Invalid port: " << port;
    return RTC_LOG_ERROR(RTCError(RTCErrorType::SYNTAX_ERROR)
                         << "ICE server parsing failed: Invalid port");
  }

  switch (service_type) {
    case ServiceType::STUN:
    case ServiceType::STUNS: {
      SocketAddress stun_address(address, port);
      if (!max_stun_servers.has_value() ||
          stun_servers->size() < *max_stun_servers) {
        stun_servers->insert(stun_address);
      } else if (!stun_servers->contains(stun_address)) {
        ignored_stun_servers.insert(stun_address);
      }
      break;
    }
    case ServiceType::TURN:
    case ServiceType::TURNS: {
      if (server.username.empty() || server.password.empty()) {
        // The WebRTC spec requires throwing an InvalidAccessError when username
        // or credential are ommitted; this is the native equivalent.
        return RTC_LOG_ERROR(
            RTCError(RTCErrorType::INVALID_PARAMETER)
            << "ICE server parsing failed: TURN server with empty "
               "username or password");
      }
      // RFC 8489 limits the size of the STUN username field to 509 characters.
      if (server.username.size() > kMaxTurnUsernameLength) {
        return RTC_LOG_ERROR(
            RTCError(RTCErrorType::INVALID_PARAMETER)
            << "ICE server parsing failed: TURN server username is too long");
      }
      // If the hostname field is not empty, then the server address must be
      // the resolved IP for that host, the hostname is needed later for TLS
      // handshake (SNI and Certificate verification).
      absl::string_view hostname =
          server.hostname.empty() ? address : server.hostname;
      SocketAddress socket_address(hostname, port);
      if (!server.hostname.empty()) {
        IPAddress ip;
        if (!IPFromString(address, &ip)) {
          // When hostname is set, the server address must be a
          // resolved ip address.
          return RTC_LOG_ERROR(
              RTCError(RTCErrorType::INVALID_PARAMETER)
              << "ICE server parsing failed: IceServer has hostname field "
                 "set, but URI does not contain an IP address.");
        }
        socket_address.SetResolvedIP(ip);
      }
      RelayServerConfig config =
          RelayServerConfig(socket_address, server.username, server.password,
                            turn_transport_type);
      if (server.tls_cert_policy ==
          PeerConnectionInterface::kTlsCertPolicyInsecureNoCheck) {
        config.tls_cert_policy =
            TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK;
      }
      config.tls_alpn_protocols = server.tls_alpn_protocols;
      config.tls_elliptic_curves = server.tls_elliptic_curves;

      turn_servers->push_back(config);
      break;
    }
    default:
      // We shouldn't get to this point with an invalid service_type, we should
      // have returned an error already.
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::INTERNAL_ERROR)
          << "ICE server parsing failed: Unexpected service type");
  }
  return RTCError::OK();
}

RTCError ParseIceServersInternal(
    const PeerConnectionInterface::IceServers& servers,
    ServerAddresses* stun_servers,
    std::vector<RelayServerConfig>* turn_servers,
    std::optional<size_t> max_stun_servers,
    ServerAddresses& ignored_stun_servers) {
  for (const PeerConnectionInterface::IceServer& server : servers) {
    if (!server.urls.empty()) {
      for (const std::string& url : server.urls) {
        if (url.empty()) {
          return RTC_LOG_ERROR(RTCError(RTCErrorType::SYNTAX_ERROR)
                               << "ICE server parsing failed: Empty uri.");
        }
        RTCError err =
            ParseIceServerUrl(server, url, stun_servers, turn_servers,
                              max_stun_servers, ignored_stun_servers);
        if (!err.ok()) {
          return err;
        }
      }
    } else if (!server.uri.empty()) {
      // Fallback to old .uri if new .urls isn't present.
      RTCError err =
          ParseIceServerUrl(server, server.uri, stun_servers, turn_servers,
                            max_stun_servers, ignored_stun_servers);

      if (!err.ok()) {
        return err;
      }
    } else {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::SYNTAX_ERROR)
                           << "ICE server parsing failed: Empty uri.");
    }
  }
  return RTCError::OK();
}

}  // namespace

RTCError ParseIceServersOrError(
    const PeerConnectionInterface::IceServers& servers,
    ServerAddresses* stun_servers,
    std::vector<RelayServerConfig>* turn_servers) {
  // Stays empty: without a maximum, no STUN servers are ignored.
  ServerAddresses ignored_stun_servers;
  return ParseIceServersInternal(servers, stun_servers, turn_servers,
                                 /*max_stun_servers=*/std::nullopt,
                                 ignored_stun_servers);
}

RTCError ParseAndValidateIceServersFromConfiguration(
    const PeerConnectionInterface::RTCConfiguration& configuration,
    ServerAddresses& stun_servers,
    std::vector<RelayServerConfig>& turn_servers) {
  RTC_DCHECK(stun_servers.empty());
  RTC_DCHECK(turn_servers.empty());
  ServerAddresses ignored_stun_servers;
  RTCError err = ParseIceServersInternal(configuration.servers, &stun_servers,
                                         &turn_servers, kMaxStunServers,
                                         ignored_stun_servers);
  if (!err.ok()) {
    return err;
  }

  if (!ignored_stun_servers.empty()) {
    RTC_LOG(LS_WARNING) << "Number of configured STUN servers is "
                        << (stun_servers.size() + ignored_stun_servers.size())
                        << " which exceeds the maximum allowed number of "
                        << kMaxStunServers;
  }

  // Restrict number of TURN servers.
  if (turn_servers.size() > kMaxTurnServers) {
    RTC_LOG(LS_WARNING) << "Number of configured TURN servers is "
                        << turn_servers.size()
                        << " which exceeds the maximum allowed number of "
                        << kMaxTurnServers;
    turn_servers.resize(kMaxTurnServers);
  }

  // Add the turn logging id to all turn servers
  for (RelayServerConfig& turn_server : turn_servers) {
    turn_server.turn_logging_id = configuration.turn_logging_id;
  }

  return RTCError::OK();
}

}  // namespace webrtc
