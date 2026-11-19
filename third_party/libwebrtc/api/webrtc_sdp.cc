/*
 *  Copyright 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/webrtc_sdp.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_view.h"
#include "api/candidate.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/payload_type.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/sctp_transport_interface.h"
#include "api/uma_metrics.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/base/rid_description.h"
#include "media/base/rtp_utils.h"
#include "media/base/stream_params.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "pc/media_protocol_names.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "pc/simulcast_sdp_serializer.h"
#include "rtc_base/base64.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_fingerprint.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/strings/string_builder.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {

namespace {
// Line type
// RFC 4566
// An SDP session description consists of a number of lines of text of
// the form:
// <type>=<value>
// where <type> MUST be exactly one case-significant character.
const int kLinePrefixLength = 2;  // Length of <type>=
const char kLineTypeVersion = 'v';
const char kLineTypeOrigin = 'o';
const char kLineTypeSessionName = 's';
const char kLineTypeSessionInfo = 'i';
const char kLineTypeSessionUri = 'u';
const char kLineTypeSessionEmail = 'e';
const char kLineTypeSessionPhone = 'p';
const char kLineTypeSessionBandwidth = 'b';
const char kLineTypeTiming = 't';
const char kLineTypeRepeatTimes = 'r';
const char kLineTypeTimeZone = 'z';
const char kLineTypeEncryptionKey = 'k';
const char kLineTypeMedia = 'm';
const char kLineTypeConnection = 'c';
const char kLineTypeAttributes = 'a';

// Attributes
constexpr absl::string_view kAttributeGroup = "group";
constexpr absl::string_view kAttributeMid = "mid";
constexpr absl::string_view kAttributeMsid = "msid";
constexpr absl::string_view kAttributeBundleOnly = "bundle-only";
constexpr absl::string_view kAttributeRtcpMux = "rtcp-mux";
constexpr absl::string_view kAttributeRtcpReducedSize = "rtcp-rsize";
constexpr absl::string_view kAttributeRtcpXr = "rtcp-xr";
constexpr absl::string_view kRtcpXrFormatRcvrRtt = "rcvr-rtt";
constexpr absl::string_view kRtcpXrFormatRcvrRttPrefix = "rcvr-rtt=";
constexpr absl::string_view kAttributeSsrc = "ssrc";
constexpr absl::string_view kSsrcAttributeCname = "cname";
constexpr absl::string_view kAttributeExtmapAllowMixed = "extmap-allow-mixed";
constexpr absl::string_view kAttributeExtmap = "extmap";
// draft-alvestrand-mmusic-msid-01
// a=msid-semantic: WMS
// This is a legacy field supported only for Plan B semantics.
constexpr absl::string_view kAttributeMsidSemantics = "msid-semantic";
constexpr absl::string_view kMediaStreamSemantic = "WMS";
constexpr absl::string_view kSsrcAttributeMsid = "msid";
constexpr absl::string_view kDefaultMsid = "default";
constexpr absl::string_view kNoStreamMsid = "-";
constexpr absl::string_view kAttributeSsrcGroup = "ssrc-group";
constexpr absl::string_view kAttributeCandidate = "candidate";
constexpr absl::string_view kAttributeFingerprint = "fingerprint";
constexpr absl::string_view kAttributeSetup = "setup";
constexpr absl::string_view kAttributeFmtp = "fmtp";
constexpr absl::string_view kAttributeRtpmap = "rtpmap";
constexpr absl::string_view kAttributeSctpmap = "sctpmap";
constexpr absl::string_view kAttributeRtcp = "rtcp";
constexpr absl::string_view kAttributeIceUfrag = "ice-ufrag";
constexpr absl::string_view kAttributeIcePwd = "ice-pwd";
constexpr absl::string_view kAttributeIceLite = "ice-lite";
constexpr absl::string_view kAttributeIceOption = "ice-options";
constexpr absl::string_view kAttributeSendOnly = "sendonly";
constexpr absl::string_view kAttributeRecvOnly = "recvonly";
constexpr absl::string_view kAttributeRtcpFb = "rtcp-fb";
constexpr absl::string_view kAttributeSendRecv = "sendrecv";
constexpr absl::string_view kAttributeInactive = "inactive";
// draft-ietf-mmusic-sctp-sdp-26
// a=sctp-port, a=max-message-size
constexpr absl::string_view kAttributeSctpPort = "sctp-port";
constexpr absl::string_view kAttributeMaxMessageSize = "max-message-size";
const int kDefaultSctpMaxMessageSize = 65536;
// 32 is a safe upper bound. Standard groups (FID, FEC-FR, SIM) use <= 3 SSRCs.
// This allows a buffer for custom semantics while preventing resource
// exhaustion.
constexpr size_t kMaxSsrcsPerGroup = 32;

// draft-hancke-tsvwg-snap
constexpr absl::string_view kAttributeSctpSnap = "sctp-init";

// draft-ietf-mmusic-sdp-simulcast-13
// a=simulcast
constexpr absl::string_view kAttributeSimulcast = "simulcast";
// draft-ietf-mmusic-rid-15
// a=rid
constexpr absl::string_view kAttributeRid = "rid";
constexpr absl::string_view kAttributePacketization = "packetization";

// https://www.rfc-editor.org/rfc/rfc9335.html
constexpr absl::string_view kAttributeCryptex = "cryptex";

// Experimental flags
constexpr absl::string_view kAttributeXGoogleFlag = "x-google-flag";
constexpr absl::string_view kValueConference = "conference";

constexpr absl::string_view kAttributeRtcpRemoteEstimate =
    "remote-net-estimate";
constexpr absl::string_view kAttributeSframe = "sframe";

// StringBuilder doesn't have a << overload for chars, while
// split and tokenize_first both take a char delimiter. To
// handle both cases these constants come in pairs of a chars and length-one
// strings.
const char kSdpDelimiterEqual[] = "=";
const char kSdpDelimiterEqualChar = '=';
const char kSdpDelimiterSpace[] = " ";
const char kSdpDelimiterSpaceChar = ' ';
constexpr absl::string_view kSdpDelimiterColon = ":";
const char kSdpDelimiterColonChar = ':';
const char kSdpDelimiterSlashChar = '/';
const char kNewLineChar = '\n';
const char kReturnChar = '\r';
const char kLineBreak[] = "\r\n";

// TODO(deadbeef): Generate the Session and Time description
// instead of hardcoding.
const char kSessionVersion[] = "v=0";
// RFC 4566
const char kSessionOriginUsername[] = "-";
const char kSessionOriginSessionId[] = "0";
const char kSessionOriginSessionVersion[] = "0";
const char kSessionOriginNettype[] = "IN";
const char kSessionOriginAddrtype[] = "IP4";
const char kSessionOriginAddress[] = "127.0.0.1";
const char kSessionName[] = "s=-";
const char kTimeDescription[] = "t=0 0";
const char kConnectionNettype[] = "IN";
const char kConnectionIpv4Addrtype[] = "IP4";
const char kConnectionIpv6Addrtype[] = "IP6";
const char kSdpMediaTypeVideo[] = "video";
const char kSdpMediaTypeAudio[] = "audio";
const char kSdpMediaTypeData[] = "application";
const char kMediaPortRejected[] = "0";
// draft-ietf-mmusic-trickle-ice-01
// When no candidates have been gathered, set the connection
// address to IP6 ::.
// TODO(perkj): FF can not parse IP6 ::. See http://crbug/430333
// Use IPV4 per default.
const char kDummyAddress[] = "0.0.0.0";
const char kDummyPort[] = "9";

const char kDefaultSctpmapProtocol[] = "webrtc-datachannel";

// RTP payload type is in the 0-127 range. Use -1 to indicate "all" payload
// types.
constexpr PayloadType kWildcardPayloadType = PayloadType::NotSet();

// Check if passed character is a "token-char" from RFC 4566.
// https://datatracker.ietf.org/doc/html/rfc4566#section-9
//    token-char =          %x21 / %x23-27 / %x2A-2B / %x2D-2E / %x30-39
//                         / %x41-5A / %x5E-7E
bool IsTokenChar(char ch) {
  return ch == 0x21 || (ch >= 0x23 && ch <= 0x27) || ch == 0x2a || ch == 0x2b ||
         ch == 0x2d || ch == 0x2e || (ch >= 0x30 && ch <= 0x39) ||
         (ch >= 0x41 && ch <= 0x5a) || (ch >= 0x5e && ch <= 0x7e);
}

void ReportSdpBandwidth(SdpBandwidthCategory category) {
  RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.SdpBandwidth", category,
                            kSdpBandwidthMax);
}

// The string fields are views into the SDP message being parsed and must not
// outlive it.
struct SsrcInfo {
  uint32_t ssrc_id = 0;
  absl::string_view cname;
  absl::string_view stream_id;
  absl::string_view track_id;
};
using SsrcInfoVec = std::vector<SsrcInfo>;
using SsrcGroupVec = std::vector<SsrcGroup>;

// The fields of a split SDP line or value, as views into it. Lines rarely have
// more than a few fields, so these are stored without a heap allocation.
using SdpFields = absl::InlinedVector<absl::string_view, 4>;

// Below ParseFailed*** functions output the line that caused the parsing
// failure and the detailed reason (`description`) of the failure to `error`.
// The functions always return false so that they can be used directly in the
// following way when error happens:
// "return ParseFailed***(...);"

// The line starting at `line_start` of `message` is the failing line.
// The reason for the failure should be provided in the `description`.
// An example of a description could be "unknown character".
bool ParseFailed(absl::string_view message,
                 size_t line_start,
                 std::string description,
                 SdpParseError* error) {
  // Get the first line of `message` from `line_start`.
  absl::string_view first_line;
  size_t line_end = message.find(kNewLineChar, line_start);
  if (line_end != std::string::npos) {
    if (line_end > 0 && (message.at(line_end - 1) == kReturnChar)) {
      --line_end;
    }
    first_line = message.substr(line_start, (line_end - line_start));
  } else {
    first_line = message.substr(line_start);
  }

  RTC_LOG(LS_ERROR) << "Failed to parse: \"" << first_line
                    << "\". Reason: " << description;
  if (error) {
    // TODO(bugs.webrtc.org/13220): In C++17, we can use plain assignment, with
    // a string_view on the right hand side.
    error->line.assign(first_line.data(), first_line.size());
    error->description = std::move(description);
  }
  return false;
}

// `line` is the failing line. The reason for the failure should be
// provided in the `description`.
bool ParseFailed(absl::string_view line,
                 std::string description,
                 SdpParseError* error) {
  return ParseFailed(line, 0, std::move(description), error);
}

// Parses failure where the failing SDP line isn't know or there are multiple
// failing lines.
bool ParseFailed(std::string description, SdpParseError* error) {
  return ParseFailed("", std::move(description), error);
}

// `line` is the failing line. The failure is due to the fact that `line`
// doesn't have `expected_fields` fields.
bool ParseFailedExpectFieldNum(absl::string_view line,
                               int expected_fields,
                               SdpParseError* error) {
  StringBuilder description;
  description << "Expects " << expected_fields << " fields.";
  return ParseFailed(line, description.Release(), error);
}

// `line` is the failing line. The failure is due to the fact that `line` has
// less than `expected_min_fields` fields.
bool ParseFailedExpectMinFieldNum(absl::string_view line,
                                  int expected_min_fields,
                                  SdpParseError* error) {
  StringBuilder description;
  description << "Expects at least " << expected_min_fields << " fields.";
  return ParseFailed(line, description.Release(), error);
}

// `line` is the failing line. The failure is due to the fact that it failed to
// get the value of `attribute`.
bool ParseFailedGetValue(absl::string_view line,
                         absl::string_view attribute,
                         SdpParseError* error) {
  StringBuilder description;
  description << "Failed to get the value of attribute: " << attribute;
  return ParseFailed(line, description.Release(), error);
}

// The line starting at `line_start` of `message` is the failing line. The
// failure is due to the line type (e.g. the "m" part of the "m-line")
// not matching what is expected. The expected line type should be
// provided as `line_type`.
bool ParseFailedExpectLine(absl::string_view message,
                           size_t line_start,
                           const char line_type,
                           absl::string_view line_value,
                           SdpParseError* error) {
  StringBuilder description;
  description << "Expect line: " << std::string(1, line_type) << "="
              << line_value;
  return ParseFailed(message, line_start, description.Release(), error);
}

bool AddLine(absl::string_view line, std::string* message) {
  if (!message)
    return false;

  message->append(line.data(), line.size());
  message->append(kLineBreak);
  return true;
}

// Trim return character, if any.
absl::string_view TrimReturnChar(absl::string_view line) {
  if (!line.empty() && line.back() == kReturnChar) {
    line.remove_suffix(1);
  }
  return line;
}

// Gets line of `message` starting at `pos`, and checks overall SDP syntax. On
// success, advances `pos` to the next line.
std::optional<absl::string_view> GetLine(absl::string_view message,
                                         size_t* pos) {
  size_t line_end = message.find(kNewLineChar, *pos);
  if (line_end == absl::string_view::npos) {
    return std::nullopt;
  }
  absl::string_view line =
      TrimReturnChar(message.substr(*pos, line_end - *pos));

  // RFC 4566
  // An SDP session description consists of a number of lines of text of
  // the form:
  // <type>=<value>
  // where <type> MUST be exactly one case-significant character and
  // <value> is structured text whose format depends on <type>.
  // Whitespace MUST NOT be used on either side of the "=" sign.
  //
  // However, an exception to the whitespace rule is made for "s=", since
  // RFC4566 also says:
  //
  //   If a session has no meaningful name, the value "s= " SHOULD be used
  //   (i.e., a single space as the session name).
  if (line.length() < 3 || !islower(static_cast<unsigned char>(line[0])) ||
      line[1] != kSdpDelimiterEqualChar ||
      (line[0] != kLineTypeSessionName && line[2] == kSdpDelimiterSpaceChar)) {
    return std::nullopt;
  }
  *pos = line_end + 1;
  return line;
}

// Init `os` to "`type`=`value`".
void InitLine(const char type, absl::string_view value, StringBuilder* os) {
  os->Clear();
  *os << type << kSdpDelimiterEqual << value;
}

// Init `os` to "a=`attribute`".
void InitAttrLine(absl::string_view attribute, StringBuilder* os) {
  os->Clear();
  *os << "a=" << attribute;
}

// Writes a SDP attribute line based on `attribute` and `value` to `message`.
void AddAttributeLine(absl::string_view attribute,
                      int value,
                      StringBuilder* os,
                      std::string* message) {
  InitAttrLine(attribute, os);
  *os << kSdpDelimiterColon << value;
  AddLine(os->str(), message);
}

bool IsLineType(absl::string_view message, const char type, size_t line_start) {
  if (message.size() < line_start + kLinePrefixLength) {
    return false;
  }
  return (message[line_start] == type &&
          message[line_start + 1] == kSdpDelimiterEqualChar);
}

bool IsLineType(absl::string_view line, const char type) {
  return IsLineType(line, type, 0);
}

std::optional<absl::string_view> GetLineWithType(absl::string_view message,
                                                 size_t* pos,
                                                 const char type) {
  if (IsLineType(message, type, *pos)) {
    return GetLine(message, pos);
  }
  return std::nullopt;
}

absl::string_view ExtractAttributeName(absl::string_view line) {
  absl::string_view rest = line.substr(kLinePrefixLength);
  for (size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] == kSdpDelimiterColonChar ||
        rest[i] == kSdpDelimiterSpaceChar) {
      return rest.substr(0, i);
    }
  }
  return rest;
}

bool AddSsrcLine(uint32_t ssrc_id,
                 absl::string_view attribute,
                 absl::string_view value,
                 StringBuilder* os,
                 std::string* message) {
  // RFC 5576
  // a=ssrc:<ssrc-id> <attribute>:<value>
  InitAttrLine(kAttributeSsrc, os);
  *os << kSdpDelimiterColon << ssrc_id << kSdpDelimiterSpace << attribute
      << kSdpDelimiterColon << value;
  return AddLine(os->str(), message);
}

// Get value only from <attribute>:<value>.
bool GetValue(absl::string_view message,
              absl::string_view attribute,
              absl::string_view* value,
              SdpParseError* error) {
  absl::string_view leftpart;
  if (!tokenize_first(message, kSdpDelimiterColonChar, &leftpart, value)) {
    return ParseFailedGetValue(message, attribute, error);
  }
  // The left part should end with the expected attribute.
  if (leftpart.length() < attribute.length() ||
      leftpart.compare(leftpart.length() - attribute.length(),
                       attribute.length(), attribute) != 0) {
    return ParseFailedGetValue(message, attribute, error);
  }
  return true;
}

bool GetValue(absl::string_view message,
              absl::string_view attribute,
              std::string* value,
              SdpParseError* error) {
  absl::string_view value_view;
  if (!GetValue(message, attribute, &value_view, error)) {
    return false;
  }
  *value = value_view;
  return true;
}

// Get a single [token] from <attribute>:<token>
bool GetSingleTokenValue(absl::string_view message,
                         absl::string_view attribute,
                         std::string* value,
                         SdpParseError* error) {
  absl::string_view value_view;
  if (!GetValue(message, attribute, &value_view, error)) {
    return false;
  }
  if (!absl::c_all_of(value_view, IsTokenChar)) {
    StringBuilder description;
    description << "Illegal character found in the value of " << attribute;
    return ParseFailed(message, description.Release(), error);
  }
  *value = value_view;
  return true;
}


template <class T>
bool GetValueFromString(absl::string_view line,
                        absl::string_view s,
                        T* t,
                        SdpParseError* error) {
  if (!FromString(s, t)) {
    StringBuilder description;
    description << "Invalid value: " << s << ".";
    return ParseFailed(line, description.Release(), error);
  }
  return true;
}

bool GetPayloadTypeFromString(absl::string_view line,
                              absl::string_view s,
                              int* payload_type,
                              SdpParseError* error) {
  return GetValueFromString(line, s, payload_type, error) &&
         IsValidRtpPayloadType(*payload_type);
}

// Creates a StreamParams track in the case when no SSRC lines are signaled.
// This is a track that does not contain SSRCs and only contains
// stream_ids/track_id if it's signaled with a=msid lines.
void CreateTrackWithNoSsrcs(const std::vector<std::string>& msid_stream_ids,
                            absl::string_view msid_track_id,
                            const std::vector<RidDescription>& rids,
                            StreamParamsVec* tracks) {
  StreamParams track;
  if (msid_track_id.empty() && rids.empty()) {
    // We only create an unsignaled track if a=msid lines were signaled.
    RTC_LOG(LS_INFO) << "MSID not signaled, skipping creation of StreamParams";
    return;
  }
  track.set_stream_ids(msid_stream_ids);
  track.id = std::string(msid_track_id);
  track.set_rids(rids);
  tracks->push_back(track);
}

// Creates the StreamParams tracks, for the case when SSRC lines are signaled.
// `msid_stream_ids` and `msid_track_id` represent the stream/track ID from the
// "a=msid" attribute, if it exists. They are empty if the attribute does not
// exist. We prioritize getting stream_ids/track_ids signaled in a=msid lines.
void CreateTracksFromSsrcInfos(const SsrcInfoVec& ssrc_infos,
                               const std::vector<std::string>& msid_stream_ids,
                               absl::string_view msid_track_id,
                               StreamParamsVec* tracks,
                               int msid_signaling) {
  RTC_DCHECK(tracks);
  if (msid_signaling & kMsidSignalingMediaSection) {
    // Unified Plan SDP msid signaling: all SSRCs in an m= section belong to a
    // single StreamParams track.
    RTC_DCHECK(!ssrc_infos.empty());
    RTC_DCHECK(tracks->empty());
    StreamParams track;
    for (const SsrcInfo& ssrc_info : ssrc_infos) {
      if (ssrc_info.cname.empty()) {
        RTC_LOG(LS_WARNING)
            << "CNAME attribute missing for SSRC " << ssrc_info.ssrc_id;
      }
      track.add_ssrc(ssrc_info.ssrc_id);
      track.cname = ssrc_info.cname;
    }
    track.set_stream_ids(msid_stream_ids);
    track.id = msid_track_id.empty() ? CreateRandomString(8)
                                     : std::string(msid_track_id);
    tracks->push_back(std::move(track));
    return;
  }

  for (const SsrcInfo& ssrc_info : ssrc_infos) {
    // According to https://tools.ietf.org/html/rfc5576#section-6.1, the CNAME
    // attribute is mandatory, but we relax that restriction.
    if (ssrc_info.cname.empty()) {
      RTC_LOG(LS_WARNING) << "CNAME attribute missing for SSRC "
                          << ssrc_info.ssrc_id;
    }
    std::vector<std::string> stream_ids;
    absl::string_view track_id;
    if (msid_signaling & kMsidSignalingSsrcAttribute) {
      // This is the case with Plan B SDP msid signaling.
      stream_ids.emplace_back(ssrc_info.stream_id);
      track_id = ssrc_info.track_id;
    } else if (msid_signaling == kMsidSignalingNotUsed) {
      // Since no media streams isn't supported with older SDP signaling, we
      // use a default stream id.
      stream_ids.emplace_back(kDefaultMsid);
    }

    auto track_it = absl::c_find_if(
        *tracks,
        [track_id](const StreamParams& track) { return track.id == track_id; });
    if (track_it == tracks->end()) {
      // If we don't find an existing track, create a new one.
      tracks->push_back(StreamParams());
      track_it = tracks->end() - 1;
    }
    StreamParams& track = *track_it;
    track.add_ssrc(ssrc_info.ssrc_id);
    track.cname = ssrc_info.cname;
    track.set_stream_ids(stream_ids);
    track.id = track_id;
  }
  for (StreamParams& stream : *tracks) {
    // If a track ID wasn't populated from the SSRC attributes OR the
    // msid attribute, use default/random values. This happens after
    // deduplication.
    if (stream.id.empty()) {
      stream.id = CreateRandomString(8);
    }
  }
}

void GetMediaStreamIds(const ContentInfo* content,
                       flat_set<std::string>* labels) {
  for (const StreamParams& stream_params :
       content->media_description()->streams()) {
    for (const std::string& stream_id : stream_params.stream_ids()) {
      labels->insert(stream_id);
    }
  }
}

// Get the default destination from the `candidates` with the given value of
// `component_id`. The default candidate should be the one most likely to work,
// typically IPv4 relay. Returns nullptr if there is no such candidate.
// `addr_type` is set to the address type to use with the returned candidate.
// RFC 5245
// The value of `component_id` currently supported are 1 (RTP) and 2 (RTCP).
// TODO(deadbeef): Decide the default destination in webrtcsession and
// pass it down via SessionDescription.
const Candidate* absl_nullable GetDefaultDestination(
    const IceCandidateCollection& candidates,
    int component_id,
    absl::string_view* addr_type) {
  *addr_type = kConnectionIpv4Addrtype;
  const Candidate* default_candidate = nullptr;
  int current_preference = 0;  // Start with lowest preference
  int current_family = AF_UNSPEC;
  for (const std::unique_ptr<IceCandidate>& jsep_candidate :
       candidates.candidates()) {
    const Candidate& candidate = jsep_candidate->candidate();
    if (candidate.component() != component_id) {
      continue;
    }
    // Default destination should be UDP only.
    if (candidate.protocol() != UDP_PROTOCOL_NAME) {
      continue;
    }
    const int preference = candidate.type_preference();
    const int family = candidate.address().ipaddr().family();
    // See if this candidate is more preferable then the current one if it's the
    // same family. Or if the current family is IPv4 already so we could safely
    // ignore all IPv6 ones. WebRTC bug 4269.
    // http://code.google.com/p/webrtc/issues/detail?id=4269
    if ((preference <= current_preference && current_family == family) ||
        (current_family == AF_INET && family == AF_INET6)) {
      continue;
    }
    if (family == AF_INET) {
      *addr_type = kConnectionIpv4Addrtype;
    } else if (family == AF_INET6) {
      *addr_type = kConnectionIpv6Addrtype;
    }
    current_preference = preference;
    current_family = family;
    default_candidate = &candidate;
  }
  return default_candidate;
}

// Writes "a=rtcp" line from default RTCP candidate in `candidates` to
// `message`.
void AddRtcpLine(const IceCandidateCollection& candidates,
                 StringBuilder* os,
                 std::string* message) {
  absl::string_view addr_type;
  const Candidate* default_candidate = GetDefaultDestination(
      candidates, ICE_CANDIDATE_COMPONENT_RTCP, &addr_type);
  // RFC 5245
  // If the agent is utilizing RTCP, it MUST encode the RTCP candidate
  // using the a=rtcp attribute as defined in RFC 3605.
  //
  // RFC 3605
  // rtcp-attribute =  "a=rtcp:" port  [nettype space addrtype space
  // connection-address] CRLF
  InitAttrLine(kAttributeRtcp, os);
  if (default_candidate != nullptr) {
    *os << kSdpDelimiterColon << default_candidate->address().port() << " "
        << kConnectionNettype << " " << addr_type << " "
        << default_candidate->address().ipaddr().ToString();
  } else {
    *os << kSdpDelimiterColon << kDummyPort << " " << kConnectionNettype << " "
        << kConnectionIpv4Addrtype << " " << kDummyAddress;
  }
  AddLine(os->str(), message);
}

bool IsValidPort(int port) {
  return port >= 0 && port <= 65535;
}

bool ParseIceOptions(absl::string_view line,
                     std::vector<std::string>* transport_options,
                     SdpParseError* error) {
  absl::string_view ice_options;
  if (!GetValue(line, kAttributeIceOption, &ice_options, error)) {
    return false;
  }
  std::vector<absl::string_view> fields =
      split(ice_options, kSdpDelimiterSpaceChar);
  for (size_t i = 0; i < fields.size(); ++i) {
    transport_options->emplace_back(fields[i]);
  }
  return true;
}

bool ParseSctpPort(absl::string_view line,
                   int* sctp_port,
                   SdpParseError* error) {
  // draft-ietf-mmusic-sctp-sdp-26
  // a=sctp-port
  const size_t expected_min_fields = 2;
  std::vector<absl::string_view> fields =
      split(line.substr(kLinePrefixLength), kSdpDelimiterColonChar);
  if (fields.size() < expected_min_fields) {
    fields = split(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar);
  }
  if (fields.size() < expected_min_fields) {
    return ParseFailedExpectMinFieldNum(line, expected_min_fields, error);
  }
  if (!FromString(fields[1], sctp_port)) {
    return ParseFailed(line, "Invalid sctp port value.", error);
  }
  if (!IsValidPort(*sctp_port)) {
    return ParseFailed(line, "Invalid sctp port value.", error);
  }
  return true;
}

bool ParseSctpMaxMessageSize(absl::string_view line,
                             int* max_message_size,
                             SdpParseError* error) {
  // draft-ietf-mmusic-sctp-sdp-26
  // a=max-message-size:199999
  const size_t expected_min_fields = 2;
  std::vector<absl::string_view> fields =
      split(line.substr(kLinePrefixLength), kSdpDelimiterColonChar);
  if (fields.size() < expected_min_fields) {
    return ParseFailedExpectMinFieldNum(line, expected_min_fields, error);
  }
  if (!FromString(fields[1], max_message_size)) {
    return ParseFailed(line, "Invalid SCTP max message size.", error);
  }
  if (*max_message_size < 0) {
    return ParseFailed(line, "Invalid SCTP max message size.", error);
  }
  return true;
}

bool ParseSctpInit(absl::string_view line,
                   std::vector<uint8_t>* cookie,
                   SdpParseError* error) {
  // draft-hancke-tsvwg-snap
  // a=sctp-init:<base64("CookieMonster")>
  absl::string_view base64_cookie;
  if (!GetValue(line, kAttributeSctpSnap, &base64_cookie, error)) {
    return false;
  }
  std::optional<std::string> decoded_cookie = Base64Decode(base64_cookie);
  if (!decoded_cookie) {
    return ParseFailed(line, "Base64 decoding of sctp-init failed.", error);
  }
  *cookie =
      std::vector<uint8_t>(decoded_cookie->begin(), decoded_cookie->end());
  return true;
}

void WriteSctpInit(const std::vector<uint8_t>& cookie, StringBuilder* os) {
  // draft-hancke-tsvwg-snap
  // a=sctp-init:<base64("CookieMonster")>
  InitAttrLine(kAttributeSctpSnap, os);
  *os << kSdpDelimiterColon << Base64Encode(cookie);
}

bool IsValidAbsoluteUri(absl::string_view uri) {
  for (size_t i = 0; i < uri.size(); ++i) {
    char c = uri[i];
    if (absl::ascii_iscntrl(c)) {
      return false;
    }
    if (c == '%') {
      if (i + 2 >= uri.size()) {
        return false;
      }
      uint8_t decoded[1];
      if (HexDecode(decoded, uri.substr(i + 1, 2)).size() != 1) {
        return false;
      }
      if (absl::ascii_iscntrl(decoded[0])) {
        return false;
      }
      i += 2;
    }
  }
  return true;
}

bool ParseExtmap(absl::string_view line,
                 RtpExtension* extmap,
                 SdpParseError* error) {
  // RFC 5285
  // a=extmap:<value>["/"<direction>] <URI> <extensionattributes>
  const auto fields =
      split<SdpFields>(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar);
  const size_t expected_min_fields = 2;
  if (fields.size() < expected_min_fields) {
    return ParseFailedExpectMinFieldNum(line, expected_min_fields, error);
  }
  absl::string_view uri = fields[1];

  absl::string_view value_direction;
  if (!GetValue(fields[0], kAttributeExtmap, &value_direction, error)) {
    return false;
  }
  const auto sub_fields =
      split<SdpFields>(value_direction, kSdpDelimiterSlashChar);
  int value = 0;
  if (!GetValueFromString(line, sub_fields[0], &value, error)) {
    return false;
  }
  std::optional<RtpHeaderExtensionId> id = RtpHeaderExtensionId::Create(value);
  if (!id.has_value()) {
    return ParseFailed(line, "Extension ID is not in valid range.", error);
  }

  bool encrypted = false;
  if (uri == RtpExtension::kEncryptHeaderExtensionsUri) {
    // RFC 6904
    // a=extmap:<value["/"<direction>] urn:ietf:params:rtp-hdrext:encrypt <URI>
    //     <extensionattributes>
    const size_t expected_min_fields_encrypted = expected_min_fields + 1;
    if (fields.size() < expected_min_fields_encrypted) {
      return ParseFailedExpectMinFieldNum(line, expected_min_fields_encrypted,
                                          error);
    }

    encrypted = true;
    uri = fields[2];
    if (uri == RtpExtension::kEncryptHeaderExtensionsUri) {
      return ParseFailed(line, "Recursive encrypted header.", error);
    }
  }

  if (!IsValidAbsoluteUri(uri)) {
    return ParseFailed(line, "URI contains invalid characters.", error);
  }

  *extmap = RtpExtension(uri, *id, encrypted);
  return true;
}

void BuildSctpContentAttributes(const MediaContentDescription* media_desc,
                                StringBuilder* os,
                                std::string* message) {
  const SctpDataContentDescription* data_desc = media_desc->as_sctp();
  if (!data_desc) {
    // Ignore unsupported media types with the SCTP protocol.
    return;
  }

  if (data_desc->use_sctpmap()) {
    // draft-ietf-mmusic-sctp-sdp-04 (legacy)
    // a=sctpmap:sctpmap-number  protocol  [streams]
    InitAttrLine(kAttributeSctpmap, os);
    *os << kSdpDelimiterColon << data_desc->port() << kSdpDelimiterSpace
        << kDefaultSctpmapProtocol << kSdpDelimiterSpace << kMaxSctpStreams;
    AddLine(os->str(), message);
  } else {
    // draft-ietf-mmusic-sctp-sdp-23
    // a=sctp-port:<port>
    InitAttrLine(kAttributeSctpPort, os);
    *os << kSdpDelimiterColon << data_desc->port();
    AddLine(os->str(), message);
    if (data_desc->max_message_size() != kDefaultSctpMaxMessageSize) {
      InitAttrLine(kAttributeMaxMessageSize, os);
      *os << kSdpDelimiterColon << data_desc->max_message_size();
      AddLine(os->str(), message);
    }
    // draft-hancke-tsvwg-snap
    auto sctp_init = data_desc->sctp_init();
    if (sctp_init.has_value()) {
      WriteSctpInit(*sctp_init, os);
      AddLine(os->str(), message);
    }
  }
}

void BuildIceUfragPwd(const TransportInfo* transport_info,
                      StringBuilder* os,
                      std::string* message) {
  RTC_DCHECK(transport_info);

  // RFC 5245
  // ice-pwd-att           = "ice-pwd" ":" password
  // ice-ufrag-att         = "ice-ufrag" ":" ufrag
  // ice-ufrag
  if (!transport_info->description.ice_ufrag.empty()) {
    InitAttrLine(kAttributeIceUfrag, os);
    *os << kSdpDelimiterColon << transport_info->description.ice_ufrag;
    AddLine(os->str(), message);
  }
  // ice-pwd
  if (!transport_info->description.ice_pwd.empty()) {
    InitAttrLine(kAttributeIcePwd, os);
    *os << kSdpDelimiterColon << transport_info->description.ice_pwd;
    AddLine(os->str(), message);
  }
}

void BuildDtlsFingerprintSetup(const TransportInfo* transport_info,
                               StringBuilder* os,
                               std::string* message) {
  RTC_DCHECK(transport_info);

  // RFC 4572
  // fingerprint-attribute  =
  //   "fingerprint" ":" hash-func SP fingerprint
  // When using max-bundle this is already included at session level.
  // Insert the fingerprint attribute.
  auto fingerprint = transport_info->description.identity_fingerprint.get();
  if (!fingerprint) {
    return;
  }
  InitAttrLine(kAttributeFingerprint, os);
  *os << kSdpDelimiterColon << fingerprint->algorithm << kSdpDelimiterSpace
      << fingerprint->GetRfc4572Fingerprint();
  AddLine(os->str(), message);

  // Inserting setup attribute.
  if (transport_info->description.connection_role != CONNECTIONROLE_NONE) {
    // Making sure we are not using "passive" mode.
    ConnectionRole role = transport_info->description.connection_role;
    std::string dtls_role_str;
    const bool success = ConnectionRoleToString(role, &dtls_role_str);
    RTC_DCHECK(success);
    InitAttrLine(kAttributeSetup, os);
    *os << kSdpDelimiterColon << dtls_role_str;
    AddLine(os->str(), message);
  }
}

void BuildMediaLine(const MediaType media_type,
                    const ContentInfo* content_info,
                    const MediaContentDescription* media_desc,
                    StringBuilder* os,
                    std::string* message) {
  // RFC 4566
  // m=<media> <port> <proto> <fmt>
  // fmt is a list of payload type numbers that MAY be used in the session.
  absl::string_view type;
  if (media_type == MediaType::AUDIO) {
    type = kSdpMediaTypeAudio;
  } else if (media_type == MediaType::VIDEO) {
    type = kSdpMediaTypeVideo;
  } else if (media_type == MediaType::DATA) {
    type = kSdpMediaTypeData;
  } else if (media_type == MediaType::UNSUPPORTED) {
    const UnsupportedContentDescription* unsupported_desc =
        media_desc->as_unsupported();
    type = unsupported_desc->media_type();
  } else {
    RTC_DCHECK_NOTREACHED();
  }

  // The port number in the m line will be updated later when associated with
  // the candidates.
  //
  // A port value of 0 indicates that the m= section is rejected.
  // RFC 3264
  // To reject an offered stream, the port number in the corresponding stream in
  // the answer MUST be set to zero.
  //
  // However, the BUNDLE draft adds a new meaning to port zero, when used along
  // with a=bundle-only.
  InitLine(kLineTypeMedia, type, os);
  *os << " ";
  if (content_info->rejected || content_info->bundle_only) {
    *os << kMediaPortRejected;
  } else if (!media_desc->connection_address().IsNil()) {
    *os << media_desc->connection_address().port();
  } else {
    *os << kDummyPort;
  }
  *os << " " << media_desc->protocol();

  // The fmt must never be empty. If no codecs are found, set the fmt attribute
  // to 0.
  if (media_type == MediaType::AUDIO || media_type == MediaType::VIDEO) {
    if (media_desc->codecs().empty()) {
      *os << " 0";
    } else {
      for (const Codec& codec : media_desc->codecs()) {
        *os << " " << codec.id;
      }
    }
  } else if (media_type == MediaType::DATA) {
    const SctpDataContentDescription* sctp_data_desc = media_desc->as_sctp();
    if (sctp_data_desc != nullptr) {
      if (sctp_data_desc->use_sctpmap()) {
        *os << " " << sctp_data_desc->port();
      } else {
        *os << " " << kDefaultSctpmapProtocol;
      }
    } else {
      RTC_DCHECK_NOTREACHED() << "Data description without SCTP";
      *os << " 0";
    }
  } else {
    *os << " 0";
  }

  AddLine(os->str(), message);
}

void BuildCandidate(const IceCandidateCollection& candidates,
                    bool include_ufrag,
                    StringBuilder* os,
                    std::string* message) {
  for (const std::unique_ptr<IceCandidate>& jsep_candidate :
       candidates.candidates()) {
    // RFC 5245
    // a=candidate:<foundation> <component-id> <transport> <priority>
    // <connection-address> <port> typ <candidate-types>
    // [raddr <connection-address>] [rport <port>]
    // *(SP extension-att-name SP extension-att-value)
    InitLine(kLineTypeAttributes,
             jsep_candidate->candidate().ToCandidateAttribute(include_ufrag),
             os);
    AddLine(os->str(), message);
  }
}

void BuildIceOptions(const std::vector<std::string>& transport_options,
                     StringBuilder* os,
                     std::string* message) {
  if (!transport_options.empty()) {
    InitAttrLine(kAttributeIceOption, os);
    *os << kSdpDelimiterColon << transport_options[0];
    for (size_t i = 1; i < transport_options.size(); ++i) {
      *os << kSdpDelimiterSpace << transport_options[i];
    }
    AddLine(os->str(), message);
  }
}

void BuildRtpHeaderExtensions(const RtpHeaderExtensions& extensions,
                              StringBuilder* os,
                              std::string* message) {
  // RFC 8285
  // a=extmap:<value>["/"<direction>] <URI> <extensionattributes>
  // The definitions MUST be either all session level or all media level. This
  // implementation uses all media level.
  for (const RtpExtension& extension : extensions) {
    InitAttrLine(kAttributeExtmap, os);
    *os << kSdpDelimiterColon << extension.id;
    if (extension.encrypt) {
      *os << kSdpDelimiterSpace << RtpExtension::kEncryptHeaderExtensionsUri;
    }
    *os << kSdpDelimiterSpace << extension.uri;
    AddLine(os->str(), message);
  }
}

bool GetMinValue(const std::vector<int>& values, int* value) {
  if (values.empty()) {
    return false;
  }
  auto it = absl::c_min_element(values);
  *value = *it;
  return true;
}

bool GetParameter(absl::string_view name,
                  const CodecParameterMap& params,
                  int* value) {
  if (params.empty()) {
    return false;
  }
  CodecParameterMap::const_iterator found = params.find(std::string(name));
  if (found == params.end()) {
    return false;
  }
  if (!FromString(found->second, value)) {
    return false;
  }
  return true;
}

void WriteFmtpHeader(PayloadType payload_type, StringBuilder* os) {
  // fmtp header: a=fmtp:`payload_type` <parameters>
  // Add a=fmtp
  InitAttrLine(kAttributeFmtp, os);
  // Add :`payload_type`
  *os << kSdpDelimiterColon << payload_type;
}

void WritePacketizationHeader(PayloadType payload_type, StringBuilder* os) {
  // packetization header: a=packetization:`payload_type` <packetization_format>
  // Add a=packetization
  InitAttrLine(kAttributePacketization, os);
  // Add :`payload_type`
  *os << kSdpDelimiterColon << payload_type;
}

void WriteRtcpFbHeader(PayloadType payload_type, StringBuilder* os) {
  // rtcp-fb header: a=rtcp-fb:`payload_type`
  // <parameters>/<ccm <ccm_parameters>>
  // Add a=rtcp-fb
  InitAttrLine(kAttributeRtcpFb, os);
  // Add :
  *os << kSdpDelimiterColon;
  if (payload_type == kWildcardPayloadType) {
    *os << "*";
  } else {
    *os << payload_type;
  }
}

void AddFmtpLine(const Codec& codec, StringBuilder* os, std::string* message) {
  if (codec.params.empty()) {
    return;
  }
  WriteFmtpHeader(codec.id, os);
  *os << kSdpDelimiterSpace;
  // Create FMTP line and check that it's nonempty.
  if (WriteFmtpParameters(codec.params, *os)) {
    AddLine(os->str(), message);
  }
}

void AddPacketizationLine(const Codec& codec,
                          StringBuilder* os,
                          std::string* message) {
  if (!codec.packetization) {
    return;
  }
  WritePacketizationHeader(codec.id, os);
  *os << " " << *codec.packetization;
  AddLine(os->str(), message);
}

void AddRtcpFbLines(const Codec& codec,
                    const FeedbackParams& skip_params,
                    StringBuilder* os,
                    std::string* message) {
  for (const FeedbackParam& param : codec.feedback_params.params()) {
    if (skip_params.Has(param)) {
      continue;
    }
    WriteRtcpFbHeader(codec.id, os);
    *os << " " << param.id();
    if (!param.param().empty()) {
      *os << " " << param.param();
    }
    AddLine(os->str(), message);
  }
}

void AddFeedbackParameter(const FeedbackParam& feedback_param, Codec* codec) {
  codec->AddFeedbackParam(feedback_param);
}

void AddFeedbackParameters(const FeedbackParams& feedback_params,
                           Codec* codec) {
  for (const FeedbackParam& param : feedback_params.params()) {
    codec->AddFeedbackParam(param);
  }
}

void BuildRtpmap(const MediaContentDescription* media_desc,
                 const MediaType media_type,
                 bool use_wildcard,
                 StringBuilder* os,
                 std::string* message) {
  RTC_DCHECK(message != nullptr);
  RTC_DCHECK(media_desc != nullptr);

  FeedbackParams common_fb_params;
  if (use_wildcard) {
    if (media_desc->rtcp_fb_ack_ccfb()) {
      common_fb_params.Add(FeedbackParam("ack", "ccfb"));
    }
    const std::vector<Codec>& codecs = media_desc->codecs();
    if (!codecs.empty()) {
      FeedbackParams intersection = codecs[0].feedback_params;
      for (size_t i = 1; i < codecs.size(); ++i) {
        intersection.Intersect(codecs[i].feedback_params);
      }
      for (const auto& param : intersection.params()) {
        common_fb_params.Add(param);
      }
    }
  }

  if (media_type == MediaType::VIDEO) {
    for (const Codec& codec : media_desc->codecs()) {
      // RFC 4566
      // a=rtpmap:<payload type> <encoding name>/<clock rate>
      // [/<encodingparameters>]
      if (codec.id != kWildcardPayloadType) {
        InitAttrLine(kAttributeRtpmap, os);
        *os << kSdpDelimiterColon << codec.id << " " << codec.name << "/"
            << kVideoCodecClockrate;
        AddLine(os->str(), message);
      }
      AddPacketizationLine(codec, os, message);
      // Include transport-wide codec parameters.
      // TODO: issues.webrtc.org/489794442 - Remove when wildcards are
      // OK to send.
      if (!use_wildcard && media_desc->rtcp_fb_ack_ccfb()) {
        Codec codec_with_ccfb = codec;
        codec_with_ccfb.feedback_params.Add({"ack", "ccfb"});
        AddRtcpFbLines(codec_with_ccfb, common_fb_params, os, message);
      } else {
        AddRtcpFbLines(codec, common_fb_params, os, message);
      }
      AddFmtpLine(codec, os, message);
    }
  } else if (media_type == MediaType::AUDIO) {
    std::vector<int> ptimes;
    std::vector<int> maxptimes;
    int max_minptime = 0;
    for (const Codec& codec : media_desc->codecs()) {
      RTC_DCHECK(!codec.name.empty());
      // RFC 4566
      // a=rtpmap:<payload type> <encoding name>/<clock rate>
      // [/<encodingparameters>]
      InitAttrLine(kAttributeRtpmap, os);
      *os << kSdpDelimiterColon << codec.id << " ";
      *os << codec.name << "/" << codec.clockrate;
      if (codec.channels != 1) {
        *os << "/" << codec.channels;
      }
      AddLine(os->str(), message);
      // Include transport-wide codec parameters.
      // TODO: issues.webrtc.org/489794442 - Remove when wildcards are
      // OK to send.
      if (!use_wildcard && media_desc->rtcp_fb_ack_ccfb()) {
        Codec codec_with_ccfb = codec;
        codec_with_ccfb.feedback_params.Add({"ack", "ccfb"});
        AddRtcpFbLines(codec_with_ccfb, common_fb_params, os, message);
      } else {
        AddRtcpFbLines(codec, common_fb_params, os, message);
      }
      AddFmtpLine(codec, os, message);
      int minptime = 0;
      if (GetParameter(kCodecParamMinPTime, codec.params, &minptime)) {
        max_minptime = std::max(minptime, max_minptime);
      }
      int ptime;
      if (GetParameter(kCodecParamPTime, codec.params, &ptime)) {
        ptimes.push_back(ptime);
      }
      int maxptime;
      if (GetParameter(kCodecParamMaxPTime, codec.params, &maxptime)) {
        maxptimes.push_back(maxptime);
      }
    }
    // Populate the maxptime attribute with the smallest maxptime of all codecs
    // under the same m-line.
    int min_maxptime = INT_MAX;
    if (GetMinValue(maxptimes, &min_maxptime)) {
      AddAttributeLine(kCodecParamMaxPTime, min_maxptime, os, message);
    }
    RTC_DCHECK_GE(min_maxptime, max_minptime);
    // Populate the ptime attribute with the smallest ptime or the largest
    // minptime, whichever is the largest, for all codecs under the same m-line.
    int ptime = INT_MAX;
    if (GetMinValue(ptimes, &ptime)) {
      ptime = std::min(ptime, min_maxptime);
      ptime = std::max(ptime, max_minptime);
      AddAttributeLine(kCodecParamPTime, ptime, os, message);
    }
  }
  if (use_wildcard) {
    for (const FeedbackParam& param : common_fb_params.params()) {
      WriteRtcpFbHeader(kWildcardPayloadType, os);
      *os << " " << param.id();
      if (!param.param().empty()) {
        *os << " " << param.param();
      }
      AddLine(os->str(), message);
    }
  }
}

void BuildRtpContentAttributes(const MediaContentDescription* media_desc,
                               const MediaType media_type,
                               int msid_signaling,
                               bool use_wildcard,
                               StringBuilder* os,
                               std::string* message) {
  SimulcastSdpSerializer serializer;
  // RFC 8285
  // a=extmap-allow-mixed
  // The attribute MUST be either on session level or media level. We support
  // responding on both levels, however, we don't respond on media level if it's
  // set on session level.
  if (media_desc->extmap_allow_mixed_level() ==
      MediaContentDescription::AttributeLevel::kMedia) {
    InitAttrLine(kAttributeExtmapAllowMixed, os);
    AddLine(os->str(), message);
  }
  if (media_desc->cryptex_level() ==
      MediaContentDescription::AttributeLevel::kMedia) {
    InitAttrLine(kAttributeCryptex, os);
    AddLine(os->str(), message);
  }
  BuildRtpHeaderExtensions(media_desc->rtp_header_extensions(), os, message);

  // RFC 3264
  // a=sendrecv || a=sendonly || a=sendrecv || a=inactive
  switch (media_desc->direction()) {
    // Special case that for sdp purposes should be treated same as inactive.
    case RtpTransceiverDirection::kStopped:
    case RtpTransceiverDirection::kInactive:
      InitAttrLine(kAttributeInactive, os);
      break;
    case RtpTransceiverDirection::kSendOnly:
      InitAttrLine(kAttributeSendOnly, os);
      break;
    case RtpTransceiverDirection::kRecvOnly:
      InitAttrLine(kAttributeRecvOnly, os);
      break;
    case RtpTransceiverDirection::kSendRecv:
      InitAttrLine(kAttributeSendRecv, os);
      break;
    default:
      RTC_DCHECK_NOTREACHED();
      InitAttrLine(kAttributeSendRecv, os);
      break;
  }
  AddLine(os->str(), message);

  // Specified in https://datatracker.ietf.org/doc/draft-ietf-mmusic-msid/16/
  // a=msid:<msid-id> <msid-appdata>
  // The msid-id is a 1*64 token char representing the media stream id, and the
  // msid-appdata is a 1*64 token char representing the track id. There is a
  // line for every media stream, with a special msid-id value of "-"
  // representing no streams. The value of "msid-appdata" MUST be identical for
  // all lines.
  if (msid_signaling & kMsidSignalingMediaSection) {
    const StreamParamsVec& streams = media_desc->streams();
    if (streams.size() == 1u) {
      const StreamParams& track = streams[0];
      const std::vector<std::string>& stream_ids = track.stream_ids();
      if (stream_ids.empty()) {
        InitAttrLine(kAttributeMsid, os);
        *os << kSdpDelimiterColon << kNoStreamMsid << kSdpDelimiterSpace
            << track.id;
        AddLine(os->str(), message);
      } else {
        for (const std::string& stream_id : stream_ids) {
          InitAttrLine(kAttributeMsid, os);
          *os << kSdpDelimiterColon << stream_id << kSdpDelimiterSpace
              << track.id;
          AddLine(os->str(), message);
        }
      }
    } else if (streams.size() > 1u) {
      RTC_LOG(LS_WARNING)
          << "Trying to serialize Unified Plan SDP with more than "
             "one track in a media section. Omitting 'a=msid'.";
    }
  }

  // RFC 5761
  // a=rtcp-mux
  if (media_desc->rtcp_mux()) {
    InitAttrLine(kAttributeRtcpMux, os);
    AddLine(os->str(), message);
  }

  // RFC 5506
  // a=rtcp-rsize
  if (media_desc->rtcp_reduced_size()) {
    InitAttrLine(kAttributeRtcpReducedSize, os);
    AddLine(os->str(), message);
  }

  // RFC 3611
  // a=rtcp-xr:rcvr-rtt=all
  if (media_desc->receive_non_sender_rtt()) {
    InitAttrLine(kAttributeRtcpXr, os);
    *os << kSdpDelimiterColon << kRtcpXrFormatRcvrRtt << "=all";
    AddLine(os->str(), message);
  }

  if (media_desc->conference_mode()) {
    InitAttrLine(kAttributeXGoogleFlag, os);
    *os << kSdpDelimiterColon << kValueConference;
    AddLine(os->str(), message);
  }

  if (media_desc->remote_estimate()) {
    InitAttrLine(kAttributeRtcpRemoteEstimate, os);
    AddLine(os->str(), message);
  }

  // draft-ietf-avtcore-rtp-sframe-02 §6
  // a=sframe
  if (media_desc->sframe_enabled()) {
    InitAttrLine(kAttributeSframe, os);
    AddLine(os->str(), message);
  }

  // RFC 4566
  // a=rtpmap:<payload type> <encoding name>/<clock rate>
  // [/<encodingparameters>]
  BuildRtpmap(media_desc, media_type, use_wildcard, os, message);

  for (const StreamParams& track : media_desc->streams()) {
    // Build the ssrc-group lines.
    for (const SsrcGroup& ssrc_group : track.ssrc_groups) {
      // RFC 5576
      // a=ssrc-group:<semantics> <ssrc-id> ...
      if (ssrc_group.ssrcs.empty()) {
        continue;
      }
      InitAttrLine(kAttributeSsrcGroup, os);
      *os << kSdpDelimiterColon << ssrc_group.semantics;
      for (uint32_t ssrc : ssrc_group.ssrcs) {
        *os << kSdpDelimiterSpace << ssrc;
      }
      AddLine(os->str(), message);
    }
    // Build the ssrc lines for each ssrc.
    for (uint32_t ssrc : track.ssrcs) {
      // RFC 5576
      // a=ssrc:<ssrc-id> cname:<value>
      AddSsrcLine(ssrc, kSsrcAttributeCname, track.cname, os, message);

      if (msid_signaling & kMsidSignalingSsrcAttribute) {
        // draft-alvestrand-mmusic-msid-00
        // a=ssrc:<ssrc-id> msid:identifier [appdata]
        // The appdata consists of the "id" attribute of a MediaStreamTrack,
        // which corresponds to the "id" attribute of StreamParams.
        // Since a=ssrc msid signaling is used in Plan B SDP semantics, and
        // multiple stream ids are not supported for Plan B, we are only adding
        // a line for the first media stream id here.
        // We use a special msid-id value of "-" to represent no streams,
        // for Unified Plan compatibility. Plan B will always have a
        // track_stream_id.
        absl::string_view first_stream_id = track.first_stream_id();
        absl::string_view stream_id =
            first_stream_id.empty() ? kNoStreamMsid : first_stream_id;
        InitAttrLine(kAttributeSsrc, os);
        *os << kSdpDelimiterColon << ssrc << kSdpDelimiterSpace
            << kSsrcAttributeMsid << kSdpDelimiterColon << stream_id
            << kSdpDelimiterSpace << track.id;
        AddLine(os->str(), message);
      }
    }

    // Build the rid lines for each layer of the track
    for (const RidDescription& rid_description : track.rids()) {
      InitAttrLine(kAttributeRid, os);
      *os << kSdpDelimiterColon
          << serializer.SerializeRidDescription(*media_desc, rid_description);
      AddLine(os->str(), message);
    }
  }

  for (const RidDescription& rid_description : media_desc->receive_rids()) {
    InitAttrLine(kAttributeRid, os);
    *os << kSdpDelimiterColon
        << serializer.SerializeRidDescription(*media_desc, rid_description);
    AddLine(os->str(), message);
  }

  // Simulcast (a=simulcast)
  // https://tools.ietf.org/html/draft-ietf-mmusic-sdp-simulcast-13#section-5.1
  if (media_desc->HasSimulcast()) {
    const auto& simulcast = media_desc->simulcast_description();
    InitAttrLine(kAttributeSimulcast, os);
    *os << kSdpDelimiterColon
        << serializer.SerializeSimulcastDescription(simulcast);
    AddLine(os->str(), message);
  }
}

void BuildMediaDescription(const ContentInfo* content_info,
                           const TransportInfo* transport_info,
                           const MediaType media_type,
                           const IceCandidateCollection& candidates,
                           int msid_signaling,
                           bool use_wildcard,
                           StringBuilder* os,
                           std::string* message) {
  RTC_DCHECK(message);
  if (!content_info) {
    return;
  }
  const MediaContentDescription* media_desc = content_info->media_description();
  RTC_DCHECK(media_desc);

  // Add the m line.
  BuildMediaLine(media_type, content_info, media_desc, os, message);
  // Add the c line.
  InitLine(kLineTypeConnection, kConnectionNettype, os);
  if (media_desc->connection_address().IsNil()) {
    *os << " " << kConnectionIpv4Addrtype << " " << kDummyAddress;
  } else if (media_desc->connection_address().family() == AF_INET) {
    *os << " " << kConnectionIpv4Addrtype << " "
        << media_desc->connection_address().ipaddr().ToString();
  } else if (media_desc->connection_address().family() == AF_INET6) {
    *os << " " << kConnectionIpv6Addrtype << " "
        << media_desc->connection_address().ipaddr().ToString();
  } else {
    *os << " " << kConnectionIpv4Addrtype << " " << kDummyAddress;
  }
  AddLine(os->str(), message);

  // RFC 4566
  // b=AS:<bandwidth> or
  // b=TIAS:<bandwidth>
  int bandwidth = media_desc->bandwidth();
  absl::string_view bandwidth_type = media_desc->bandwidth_type();
  if (bandwidth_type == kApplicationSpecificBandwidth && bandwidth >= 1000) {
    InitLine(kLineTypeSessionBandwidth, bandwidth_type, os);
    bandwidth /= 1000;
    *os << kSdpDelimiterColon << bandwidth;
    AddLine(os->str(), message);
  } else if (bandwidth_type == kTransportSpecificBandwidth && bandwidth > 0) {
    InitLine(kLineTypeSessionBandwidth, bandwidth_type, os);
    *os << kSdpDelimiterColon << bandwidth;
    AddLine(os->str(), message);
  }

  // Add the a=bundle-only line.
  if (content_info->bundle_only) {
    InitAttrLine(kAttributeBundleOnly, os);
    AddLine(os->str(), message);
  }

  // Add the a=rtcp line.
  if (IsRtpProtocol(media_desc->protocol())) {
    AddRtcpLine(candidates, os, message);
  }

  // Build the a=candidate lines. We don't include ufrag and pwd in the
  // candidates in the SDP to avoid redundancy.
  BuildCandidate(candidates, false, os, message);

  // Use the transport_info to build the media level ice-ufrag, ice-pwd
  // and DTLS fingerprint and setup attributes.
  if (transport_info) {
    BuildIceUfragPwd(transport_info, os, message);

    // draft-petithuguenin-mmusic-ice-attributes-level-03
    BuildIceOptions(transport_info->description.transport_options, os, message);

    // Also include the DTLS fingerprint and setup attribute if available.
    BuildDtlsFingerprintSetup(transport_info, os, message);
  }

  // RFC 3388
  // mid-attribute      = "a=mid:" identification-tag
  // identification-tag = token
  // Use the content name as the mid identification-tag.
  InitAttrLine(kAttributeMid, os);
  *os << kSdpDelimiterColon << content_info->mid();
  AddLine(os->str(), message);

  if (IsDtlsSctp(media_desc->protocol())) {
    BuildSctpContentAttributes(media_desc, os, message);
  } else if (IsRtpProtocol(media_desc->protocol())) {
    BuildRtpContentAttributes(media_desc, media_type, msid_signaling,
                              use_wildcard, os, message);
  }
}

bool ParseConnectionData(absl::string_view line,
                         SocketAddress* addr,
                         SdpParseError* error) {
  // Parse the line from left to right.
  absl::string_view token;
  absl::string_view rightpart;
  // RFC 4566
  // c=<nettype> <addrtype> <connection-address>
  // Skip the "c="
  if (!tokenize_first(line, kSdpDelimiterEqualChar, &token, &rightpart)) {
    return ParseFailed(line, "Failed to parse the network type.", error);
  }

  // Extract and verify the <nettype>
  if (!tokenize_first(rightpart, kSdpDelimiterSpaceChar, &token, &rightpart) ||
      token != kConnectionNettype) {
    return ParseFailed(line,
                       "Failed to parse the connection data. The network type "
                       "is not currently supported.",
                       error);
  }

  // Extract the "<addrtype>" and "<connection-address>".
  if (!tokenize_first(rightpart, kSdpDelimiterSpaceChar, &token, &rightpart)) {
    return ParseFailed(line, "Failed to parse the address type.", error);
  }

  // The rightpart part should be the IP address without the slash which is used
  // for multicast.
  if (rightpart.find('/') != absl::string_view::npos) {
    return ParseFailed(line,
                       "Failed to parse the connection data. Multicast is not "
                       "currently supported.",
                       error);
  }
  addr->SetIP(rightpart);

  // Verify that the addrtype matches the type of the parsed address.
  if ((addr->family() == AF_INET && token != "IP4") ||
      (addr->family() == AF_INET6 && token != "IP6")) {
    addr->Clear();
    return ParseFailed(
        line,
        "Failed to parse the connection data. The address type is mismatching.",
        error);
  }
  return true;
}

bool ParseGroupAttribute(absl::string_view line,
                         SessionDescription* desc,
                         SdpParseError* error) {
  RTC_DCHECK(desc != nullptr);

  // RFC 5888 and RFC 8843.
  // a=group:BUNDLE video voice
  std::vector<absl::string_view> fields =
      split(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar);
  absl::string_view semantics;
  if (!GetValue(fields[0], kAttributeGroup, &semantics, error)) {
    return ParseFailed(line, "Failed to parse group attribute.", error);
  }
  for (size_t i = 1; i < fields.size(); ++i) {
    if (!absl::c_all_of(fields[i], IsTokenChar)) {
      return ParseFailed(line, "Failed to parse group tag.", error);
    }
  }
  ContentGroup group{std::string(semantics)};
  for (size_t i = 1; i < fields.size(); ++i) {
    group.AddContentName(fields[i]);
  }
  desc->AddGroup(group);
  return true;
}

bool ParseFingerprintAttribute(absl::string_view line,
                               std::unique_ptr<SSLFingerprint>* fingerprint,
                               SdpParseError* error) {
  std::vector<absl::string_view> fields =
      split(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar);
  const size_t expected_fields = 2;
  if (fields.size() != expected_fields) {
    return ParseFailedExpectFieldNum(line, expected_fields, error);
  }

  // The first field here is "fingerprint:<hash>.
  std::string algorithm;
  if (!GetValue(fields[0], kAttributeFingerprint, &algorithm, error)) {
    return false;
  }

  // Downcase the algorithm. Note that we don't need to downcase the
  // fingerprint because hex_decode can handle upper-case.
  absl::c_transform(algorithm, algorithm.begin(), ::tolower);

  // The second field is the digest value. De-hexify it.
  *fingerprint = SSLFingerprint::CreateFromRfc4572(algorithm, fields[1]);
  if (*fingerprint == nullptr) {
    return ParseFailed(line, "Failed to create fingerprint from the digest.",
                       error);
  }

  return true;
}

bool ParseDtlsSetup(absl::string_view line,
                    ConnectionRole* role_ptr,
                    SdpParseError* error) {
  // setup-attr           =  "a=setup:" role
  // role                 =  "active" / "passive" / "actpass" / "holdconn"
  const auto fields =
      split<SdpFields>(line.substr(kLinePrefixLength), kSdpDelimiterColonChar);
  const size_t expected_fields = 2;
  if (fields.size() != expected_fields) {
    return ParseFailedExpectFieldNum(line, expected_fields, error);
  }
  if (std::optional<ConnectionRole> role = StringToConnectionRole(fields[1]);
      role.has_value()) {
    *role_ptr = *role;
    return true;
  }
  return ParseFailed(line, "Invalid attribute value.", error);
}

bool ParseSessionDescription(absl::string_view message,
                             size_t* pos,
                             std::string* session_id,
                             std::string* session_version,
                             TransportDescription* session_td,
                             RtpHeaderExtensions* session_extmaps,
                             SocketAddress* connection_addr,
                             SessionDescription* desc,
                             SdpParseError* error) {
  std::optional<absl::string_view> line;

  desc->set_msid_signaling(kMsidSignalingNotUsed);
  desc->set_extmap_allow_mixed(false);
  desc->set_cryptex(false);

  // RFC 4566
  // v=  (protocol version)
  line = GetLineWithType(message, pos, kLineTypeVersion);
  if (!line) {
    return ParseFailedExpectLine(message, *pos, kLineTypeVersion, std::string(),
                                 error);
  }
  // RFC 4566
  // o=<username> <sess-id> <sess-version> <nettype> <addrtype>
  // <unicast-address>
  line = GetLineWithType(message, pos, kLineTypeOrigin);
  if (!line) {
    return ParseFailedExpectLine(message, *pos, kLineTypeOrigin, std::string(),
                                 error);
  }
  std::vector<absl::string_view> fields =
      split(line->substr(kLinePrefixLength), kSdpDelimiterSpaceChar);
  const size_t expected_fields = 6;
  if (fields.size() != expected_fields) {
    return ParseFailedExpectFieldNum(*line, expected_fields, error);
  }
  *session_id = std::string(fields[1]);
  *session_version = std::string(fields[2]);

  // RFC 4566
  // s=  (session name)
  line = GetLineWithType(message, pos, kLineTypeSessionName);
  if (!line) {
    return ParseFailedExpectLine(message, *pos, kLineTypeSessionName,
                                 std::string(), error);
  }

  // optional lines
  // Those are the optional lines, so shouldn't return false if not present.
  // RFC 4566
  // i=* (session information)
  GetLineWithType(message, pos, kLineTypeSessionInfo);

  // RFC 4566
  // u=* (URI of description)
  GetLineWithType(message, pos, kLineTypeSessionUri);

  // RFC 4566
  // e=* (email address)
  GetLineWithType(message, pos, kLineTypeSessionEmail);

  // RFC 4566
  // p=* (phone number)
  GetLineWithType(message, pos, kLineTypeSessionPhone);

  // RFC 4566
  // c=* (connection information -- not required if included in
  //      all media)
  if (std::optional<absl::string_view> cline =
          GetLineWithType(message, pos, kLineTypeConnection);
      cline.has_value()) {
    if (!ParseConnectionData(*cline, connection_addr, error)) {
      return false;
    }
  }

  // RFC 4566
  // b=* (zero or more bandwidth information lines)
  while (GetLineWithType(message, pos, kLineTypeSessionBandwidth).has_value()) {
    // By pass zero or more b lines.
  }

  // RFC 4566
  // One or more time descriptions ("t=" and "r=" lines; see below)
  // t=  (time the session is active)
  // r=* (zero or more repeat times)
  // Ensure there's at least one time description
  if (!GetLineWithType(message, pos, kLineTypeTiming).has_value()) {
    return ParseFailedExpectLine(message, *pos, kLineTypeTiming, std::string(),
                                 error);
  }

  while (GetLineWithType(message, pos, kLineTypeRepeatTimes).has_value()) {
    // By pass zero or more r lines.
  }

  // Go through the rest of the time descriptions
  while (GetLineWithType(message, pos, kLineTypeTiming).has_value()) {
    while (GetLineWithType(message, pos, kLineTypeRepeatTimes).has_value()) {
      // By pass zero or more r lines.
    }
  }

  // RFC 4566
  // z=* (time zone adjustments)
  GetLineWithType(message, pos, kLineTypeTimeZone);

  // RFC 4566
  // k=* (encryption key)
  GetLineWithType(message, pos, kLineTypeEncryptionKey);

  // RFC 4566
  // a=* (zero or more session attribute lines)
  while (std::optional<absl::string_view> aline =
             GetLineWithType(message, pos, kLineTypeAttributes)) {
    absl::string_view attr = ExtractAttributeName(*aline);
    if (attr == kAttributeGroup) {
      if (!ParseGroupAttribute(*aline, desc, error)) {
        return false;
      }
    } else if (attr == kAttributeIceUfrag) {
      if (!GetValue(*aline, kAttributeIceUfrag, &(session_td->ice_ufrag),
                    error)) {
        return false;
      }
    } else if (attr == kAttributeIcePwd) {
      if (!GetValue(*aline, kAttributeIcePwd, &(session_td->ice_pwd), error)) {
        return false;
      }
    } else if (attr == kAttributeIceLite) {
      session_td->ice_mode = ICEMODE_LITE;
    } else if (attr == kAttributeIceOption) {
      if (!ParseIceOptions(*aline, &(session_td->transport_options), error)) {
        return false;
      }
    } else if (attr == kAttributeFingerprint) {
      if (session_td->identity_fingerprint) {
        return ParseFailed(
            *aline,
            "Can't have multiple fingerprint attributes at the same level.",
            error);
      }
      std::unique_ptr<SSLFingerprint> fingerprint;
      if (!ParseFingerprintAttribute(*aline, &fingerprint, error)) {
        return false;
      }
      session_td->identity_fingerprint = std::move(fingerprint);
    } else if (attr == kAttributeSetup) {
      if (!ParseDtlsSetup(*aline, &(session_td->connection_role), error)) {
        return false;
      }
    } else if (attr == kAttributeMsidSemantics) {
      absl::string_view semantics;
      if (!GetValue(*aline, kAttributeMsidSemantics, &semantics, error)) {
        return false;
      }
      if (absl::StrContainsIgnoreCase(semantics, kMediaStreamSemantic)) {
        desc->set_msid_signaling(kMsidSignalingSemantic);
      }
    } else if (attr == kAttributeExtmapAllowMixed) {
      desc->set_extmap_allow_mixed(true);
    } else if (attr == kAttributeExtmap) {
      RtpExtension extmap;
      if (!ParseExtmap(*aline, &extmap, error)) {
        return false;
      }
      session_extmaps->push_back(extmap);
    } else if (attr == kAttributeCryptex) {
      desc->set_cryptex(true);
    }
  }
  return true;
}

bool ParseMsidAttribute(absl::string_view line,
                        std::vector<std::string>* stream_ids,
                        std::string* track_id,
                        SdpParseError* error) {
  // https://datatracker.ietf.org/doc/rfc8830/
  // a=msid:<msid-value>
  // msid-value = msid-id [ SP msid-appdata ]
  // msid-id = 1*64token-char ; see RFC 4566
  // msid-appdata = 1*64token-char  ; see RFC 4566
  // Note that JSEP stipulates not sending msid-appdata so
  // a=msid:<stream id> <track id>
  // is supported for backward compability reasons only.
  // RFC 8830 section 2 states that duplicate a=msid:stream track is illegal.
  std::vector<absl::string_view> fields;
  size_t num_fields =
      tokenize(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar, &fields);
  if (num_fields < 1 || num_fields > 2) {
    return ParseFailed(line, "Expected a stream ID and optionally a track ID",
                       error);
  }
  if (num_fields == 1) {
    if (line.back() == kSdpDelimiterSpaceChar) {
      return ParseFailed(line, "Missing track ID in msid attribute.", error);
    }
    // Keep any previously parsed `track_id`. Ending with an empty string track
    // will cause a random track id to be generated later in the process.
  } else {
    // All track ids should be the same within an m section in a Unified Plan
    // SDP.
    if (!track_id->empty() && *track_id != fields[1]) {
      return ParseFailed(
          line, "Two different track IDs in msid attribute in one m= section",
          error);
    }
    *track_id = fields[1];
  }

  // msid:<msid-id>
  absl::string_view new_stream_id;
  if (!GetValue(fields[0], kAttributeMsid, &new_stream_id, error)) {
    return false;
  }
  if (new_stream_id.empty()) {
    return ParseFailed(line, "Missing stream ID in msid attribute.", error);
  }
  // The special value "-" indicates "no MediaStream".
  if (new_stream_id != kNoStreamMsid &&
      !absl::c_any_of(*stream_ids,
                      [&new_stream_id](const std::string& existing_stream_id) {
                        return new_stream_id == existing_stream_id;
                      })) {
    stream_ids->emplace_back(new_stream_id);
  }
  return true;
}

void RemoveDuplicateRidDescriptions(const std::vector<int>& payload_types,
                                    std::vector<RidDescription>* rids) {
  RTC_DCHECK(rids);
  if (rids->size() <= 1) {
    return;
  }
  flat_set<std::string> to_remove;
  flat_set<absl::string_view> unique_rids;
  // Find duplicate RIDs to remove.
  for (const RidDescription& rid : *rids) {
    if (!unique_rids.insert(rid.rid).second) {
      to_remove.insert(rid.rid);
    }
  }
  // Remove every rid description that appears in the to_remove list.
  if (!to_remove.empty()) {
    std::erase_if(*rids, [&to_remove](const RidDescription& rid) {
      return to_remove.contains(rid.rid);
    });
  }
}

// Create a new list (because SimulcastLayerList is immutable) without any
// layers that have a rid in the to_remove list.
// If a group of alternatives is empty after removing layers, the group should
// be removed altogether.
SimulcastLayerList RemoveRidsFromSimulcastLayerList(
    const flat_set<absl::string_view>& to_remove,
    const SimulcastLayerList& layers) {
  SimulcastLayerList result;
  for (const std::vector<SimulcastLayer>& vector : layers) {
    std::vector<SimulcastLayer> new_layers;
    for (const SimulcastLayer& layer : vector) {
      if (!to_remove.contains(layer.rid)) {
        new_layers.push_back(layer);
      }
    }
    // If all layers were removed, do not add an entry.
    if (!new_layers.empty()) {
      result.AddLayerWithAlternatives(new_layers);
    }
  }

  return result;
}

// Will remove Simulcast Layers if:
// 1. They appear in both send and receive directions.
// 2. They do not appear in the list of `valid_rids`.
void RemoveInvalidRidsFromSimulcast(
    const std::vector<RidDescription>& valid_rids,
    SimulcastDescription* absl_nonnull simulcast) {
  RTC_DCHECK(simulcast);
  std::vector<SimulcastLayer> all_send_layers =
      simulcast->send_layers().GetAllLayers();
  std::vector<SimulcastLayer> all_receive_layers =
      simulcast->receive_layers().GetAllLayers();
  // `to_remove` must be declared after the vectors because it holds views.
  flat_set<absl::string_view> to_remove;

  // If a rid appears in both send and receive directions, remove it from both.
  // This algorithm runs in O(n^2) time, but for small n (as is the case with
  // simulcast layers) it should still perform well.
  for (const SimulcastLayer& send_layer : all_send_layers) {
    if (absl::c_any_of(all_receive_layers,
                       [&send_layer](const SimulcastLayer& layer) {
                         return layer.rid == send_layer.rid;
                       })) {
      to_remove.insert(send_layer.rid);
    }
  }

  // Add any rid that is not in the valid list to the remove set.
  for (const SimulcastLayer& send_layer : all_send_layers) {
    if (absl::c_none_of(valid_rids, [&send_layer](const RidDescription& rid) {
          return send_layer.rid == rid.rid &&
                 rid.direction == RidDirection::kSend;
        })) {
      to_remove.insert(send_layer.rid);
    }
  }

  // Add any rid that is not in the valid list to the remove set.
  for (const SimulcastLayer& receive_layer : all_receive_layers) {
    if (absl::c_none_of(valid_rids,
                        [&receive_layer](const RidDescription& rid) {
                          return receive_layer.rid == rid.rid &&
                                 rid.direction == RidDirection::kReceive;
                        })) {
      to_remove.insert(receive_layer.rid);
    }
  }

  simulcast->send_layers() =
      RemoveRidsFromSimulcastLayerList(to_remove, simulcast->send_layers());
  simulcast->receive_layers() =
      RemoveRidsFromSimulcastLayerList(to_remove, simulcast->receive_layers());
}

// RFC 3551
//  PT   encoding    media type  clock rate   channels
//                      name                    (Hz)
//  0    PCMU        A            8,000       1
//  1    reserved    A
//  2    reserved    A
//  3    GSM         A            8,000       1
//  4    G723        A            8,000       1
//  5    DVI4        A            8,000       1
//  6    DVI4        A           16,000       1
//  7    LPC         A            8,000       1
//  8    PCMA        A            8,000       1
//  9    G722        A            8,000       1
//  10   L16         A           44,100       2
//  11   L16         A           44,100       1
//  12   QCELP       A            8,000       1
//  13   CN          A            8,000       1
//  14   MPA         A           90,000       (see text)
//  15   G728        A            8,000       1
//  16   DVI4        A           11,025       1
//  17   DVI4        A           22,050       1
//  18   G729        A            8,000       1
struct StaticPayloadAudioCodec {
  const char* name;
  int clockrate;
  size_t channels;
};
const StaticPayloadAudioCodec kStaticPayloadAudioCodecs[] = {
    {.name = "PCMU", .clockrate = 8000, .channels = 1},
    {.name = "x-illegal-value-1", .clockrate = 8000, .channels = 0},
    {.name = "x-illegal-value-2", .clockrate = 8000, .channels = 0},
    {.name = "GSM", .clockrate = 8000, .channels = 1},
    {.name = "G723", .clockrate = 8000, .channels = 1},
    {.name = "DVI4", .clockrate = 8000, .channels = 1},
    {.name = "DVI4", .clockrate = 16000, .channels = 1},
    {.name = "LPC", .clockrate = 8000, .channels = 1},
    {.name = "PCMA", .clockrate = 8000, .channels = 1},
    {.name = "G722", .clockrate = 8000, .channels = 1},
    {.name = "L16", .clockrate = 44100, .channels = 2},
    {.name = "L16", .clockrate = 44100, .channels = 1},
    {.name = "QCELP", .clockrate = 8000, .channels = 1},
    {.name = "CN", .clockrate = 8000, .channels = 1},
    {.name = "MPA", .clockrate = 90000, .channels = 1},
    {.name = "G728", .clockrate = 8000, .channels = 1},
    {.name = "DVI4", .clockrate = 11025, .channels = 1},
    {.name = "DVI4", .clockrate = 22050, .channels = 1},
    {.name = "G729", .clockrate = 8000, .channels = 1},
};

void MaybeCreateStaticPayloadAudioCodecs(const std::vector<int>& fmts,
                                         MediaContentDescription* media_desc) {
  if (!media_desc) {
    return;
  }
  RTC_DCHECK(media_desc->codecs().empty());
  for (int payload_type : fmts) {
    if (!media_desc->HasCodec(payload_type) && payload_type >= 0 &&
        payload_type < std::ssize(kStaticPayloadAudioCodecs)) {
      std::string encoding_name = kStaticPayloadAudioCodecs[payload_type].name;
      int clock_rate = kStaticPayloadAudioCodecs[payload_type].clockrate;
      size_t channels = kStaticPayloadAudioCodecs[payload_type].channels;
      media_desc->AddCodec(
          CreateAudioCodec(payload_type, encoding_name, clock_rate, channels));
    }
  }
}

void BackfillCodecParameters(std::vector<Codec>& codecs) {
  for (auto& codec : codecs) {
    std::string unused_value;
    if (absl::EqualsIgnoreCase(kVp9CodecName, codec.name)) {
      // https://datatracker.ietf.org/doc/html/draft-ietf-payload-vp9#section-6
      // profile-id defaults to "0"
      if (!codec.GetParam(kVP9ProfileId, &unused_value)) {
        codec.SetParam(kVP9ProfileId, "0");
      }
    } else if (absl::EqualsIgnoreCase(kH264CodecName, codec.name)) {
      // https://www.rfc-editor.org/rfc/rfc6184#section-6.2
      // packetization-mode defaults to "0"
      if (!codec.GetParam(kH264FmtpPacketizationMode, &unused_value)) {
        codec.SetParam(kH264FmtpPacketizationMode, "0");
      }
    } else if (absl::EqualsIgnoreCase(kAv1CodecName, codec.name)) {
      // https://aomediacodec.github.io/av1-rtp-spec/#72-sdp-parameters
      if (!codec.GetParam(kAv1FmtpProfile, &unused_value)) {
        codec.SetParam(kAv1FmtpProfile, "0");
      }
      if (!codec.GetParam(kAv1FmtpLevelIdx, &unused_value)) {
        codec.SetParam(kAv1FmtpLevelIdx, "5");
      }
      if (!codec.GetParam(kAv1FmtpTier, &unused_value)) {
        codec.SetParam(kAv1FmtpTier, "0");
      }
    } else if (absl::EqualsIgnoreCase(kH265CodecName, codec.name)) {
      // https://datatracker.ietf.org/doc/html/draft-aboba-avtcore-hevc-webrtc
      if (!codec.GetParam(kH265FmtpLevelId, &unused_value)) {
        codec.SetParam(kH265FmtpLevelId, "93");
      }
      if (!codec.GetParam(kH265FmtpTxMode, &unused_value)) {
        codec.SetParam(kH265FmtpTxMode, "SRST");
      }
    }
  }
}

// Gets the current codec setting associated with `payload_type`. If there
// is no Codec associated with that payload type it adds an empty codec
// with that payload type and returns a reference to it.
Codec& GetOrAddCodecWithPayloadType(MediaContentDescription* content_desc,
                                    PayloadType payload_type) {
  std::vector<Codec>& codecs = content_desc->mutable_codecs();
  for (Codec& codec : codecs) {
    if (codec.id == payload_type) {
      return codec;
    }
  }
  if (content_desc->type() == MediaType::AUDIO) {
    codecs.push_back(
        CreateAudioCodec(payload_type, "", kDefaultAudioClockRateHz, 0));
  } else {
    codecs.push_back(CreateVideoCodec(payload_type, ""));
  }
  return codecs.back();
}

// Adds or updates existing codec corresponding to `payload_type` according
// to `feedback_param`.
void UpdateCodec(MediaContentDescription* content_desc,
                 PayloadType payload_type,
                 const FeedbackParam& feedback_param) {
  // Codec might already have been populated (from rtpmap).
  Codec& codec = GetOrAddCodecWithPayloadType(content_desc, payload_type);
  AddFeedbackParameter(feedback_param, &codec);
}

bool ParseFmtpAttributes(absl::string_view line,
                         const MediaType media_type,
                         MediaContentDescription* media_desc,
                         SdpParseError* error) {
  if (media_type != MediaType::AUDIO && media_type != MediaType::VIDEO) {
    return true;
  }

  absl::string_view line_payload;
  absl::string_view line_params;

  // https://tools.ietf.org/html/rfc4566#section-6
  // a=fmtp:<format> <format specific parameters>
  // At least two fields, whereas the second one is any of the optional
  // parameters.
  if (!tokenize_first(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar,
                      &line_payload, &line_params)) {
    ParseFailedExpectMinFieldNum(line, 2, error);
    return false;
  }

  // Parse out the payload information.
  absl::string_view payload_type_str;
  if (!GetValue(line_payload, kAttributeFmtp, &payload_type_str, error)) {
    return false;
  }

  int pt_int = 0;
  if (!GetPayloadTypeFromString(line_payload, payload_type_str, &pt_int,
                                error)) {
    return false;
  }
  PayloadType payload_type = PayloadType(pt_int);

  // Parse out format specific parameters directly into the codec's parameter
  // map.
  Codec& codec = GetOrAddCodecWithPayloadType(media_desc, payload_type);
  if (!ParseFmtpParameterSet(line_params, codec.params).ok()) {
    return false;
  }

  return true;
}

bool ParseSsrcGroupAttribute(absl::string_view line,
                             SsrcGroupVec* ssrc_groups,
                             SdpParseError* error) {
  RTC_DCHECK(ssrc_groups != nullptr);
  // RFC 5576
  // a=ssrc-group:<semantics> <ssrc-id> ...
  std::vector<absl::string_view> fields =
      split(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar);
  const size_t expected_min_fields = 2;
  if (fields.size() < expected_min_fields) {
    return ParseFailedExpectMinFieldNum(line, expected_min_fields, error);
  }
  // fields[0] is semantics, remaining fields are SSRCs.
  if (fields.size() - 1 > kMaxSsrcsPerGroup) {
    return ParseFailed(line, "Too many SSRCs in ssrc-group", error);
  }
  absl::string_view semantics;
  if (!GetValue(fields[0], kAttributeSsrcGroup, &semantics, error)) {
    return false;
  }
  std::vector<uint32_t> ssrcs;
  ssrcs.reserve(fields.size() - 1);
  for (size_t i = 1; i < fields.size(); ++i) {
    uint32_t ssrc = 0;
    if (!GetValueFromString(line, fields[i], &ssrc, error)) {
      return false;
    }
    // Reject duplicates. While not forbidden by RFC 5576,
    // they don't make sense.
    if (absl::c_linear_search(ssrcs, ssrc)) {
      return ParseFailed(line, "Duplicate SSRC in ssrc-group", error);
    }
    ssrcs.push_back(ssrc);
  }
  ssrc_groups->push_back(SsrcGroup(semantics, ssrcs));
  return true;
}

bool ParseSsrcAttribute(absl::string_view line,
                        SsrcInfoVec* ssrc_infos,
                        int* msid_signaling,
                        SdpParseError* error) {
  RTC_DCHECK(ssrc_infos != nullptr);
  // RFC 5576
  // a=ssrc:<ssrc-id> <attribute>
  // a=ssrc:<ssrc-id> <attribute>:<value>
  absl::string_view field1, field2;
  if (!tokenize_first(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar,
                      &field1, &field2)) {
    const size_t expected_fields = 2;
    return ParseFailedExpectFieldNum(line, expected_fields, error);
  }

  // ssrc:<ssrc-id>
  absl::string_view ssrc_id_s;
  if (!GetValue(field1, kAttributeSsrc, &ssrc_id_s, error)) {
    return false;
  }
  uint32_t ssrc_id = 0;
  if (!GetValueFromString(line, ssrc_id_s, &ssrc_id, error)) {
    return false;
  }

  absl::string_view attribute;
  absl::string_view value;
  if (!tokenize_first(field2, kSdpDelimiterColonChar, &attribute, &value)) {
    StringBuilder description;
    description << "Failed to get the ssrc attribute value from " << field2
                << ". Expected format <attribute>:<value>.";
    return ParseFailed(line, description.Release(), error);
  }

  // Check if there's already an item for this `ssrc_id`. Create a new one if
  // there isn't.
  auto ssrc_info_it =
      absl::c_find_if(*ssrc_infos, [ssrc_id](const SsrcInfo& ssrc_info) {
        return ssrc_info.ssrc_id == ssrc_id;
      });
  if (ssrc_info_it == ssrc_infos->end()) {
    SsrcInfo info;
    info.ssrc_id = ssrc_id;
    ssrc_infos->push_back(info);
    ssrc_info_it = ssrc_infos->end() - 1;
  }
  SsrcInfo& ssrc_info = *ssrc_info_it;

  // Store the info to the `ssrc_info`.
  if (attribute == kSsrcAttributeCname) {
    // RFC 5576
    // cname:<value>
    ssrc_info.cname = value;
  } else if (attribute == kSsrcAttributeMsid) {
    // draft-alvestrand-mmusic-msid-00
    // msid:identifier [appdata]
    const auto fields = split<SdpFields>(value, kSdpDelimiterSpaceChar);
    if (fields.empty() || fields.size() > 2) {
      return ParseFailed(
          line, "Expected format \"msid:<identifier>[ <appdata>]\".", error);
    }
    ssrc_info.stream_id = fields[0];
    if (fields.size() == 2) {
      ssrc_info.track_id = fields[1];
    }
    *msid_signaling |= kMsidSignalingSsrcAttribute;
  } else {
    RTC_LOG(LS_INFO) << "Ignored unknown ssrc-specific attribute: " << line;
  }
  return true;
}

// Updates or creates a new codec entry in the audio description with according
// to `name`, `clockrate`, `bitrate`, and `channels`.
void UpdateCodec(PayloadType payload_type,
                 absl::string_view name,
                 int clockrate,
                 int bitrate,
                 size_t channels,
                 MediaContentDescription* desc) {
  // Codec may already be populated with (only) optional parameters
  // (from an fmtp).
  Codec& codec = GetOrAddCodecWithPayloadType(desc, payload_type);
  codec.name = std::string(name);
  codec.clockrate = clockrate;
  codec.bitrate = bitrate;
  codec.channels = channels;
}

// Updates or creates a new codec entry in the video description according to
// `name`, `width`, `height`, and `framerate`.
void UpdateCodec(PayloadType payload_type,
                 absl::string_view name,
                 MediaContentDescription* desc) {
  // Codec may already be populated with (only) optional parameters
  // (from an fmtp).
  Codec& codec = GetOrAddCodecWithPayloadType(desc, payload_type);
  codec.name = std::string(name);
}

bool ParseRtpmapAttribute(absl::string_view line,
                          const MediaType media_type,
                          const std::vector<int>& payload_types,
                          MediaContentDescription* media_desc,
                          SdpParseError* error) {
  static const int kFirstDynamicPayloadTypeLowerRange = 35;
  // RFC 4566
  // a=rtpmap:<payload type> <encoding name>/<clock rate>[/<encodingparameters>]
  const auto fields =
      split<SdpFields>(line.substr(kLinePrefixLength), kSdpDelimiterSpaceChar);
  const size_t expected_min_fields = 2;
  if (fields.size() < expected_min_fields) {
    return ParseFailedExpectMinFieldNum(line, expected_min_fields, error);
  }
  absl::string_view payload_type_value;
  if (!GetValue(fields[0], kAttributeRtpmap, &payload_type_value, error)) {
    return false;
  }
  int payload_type = 0;
  if (!GetPayloadTypeFromString(line, payload_type_value, &payload_type,
                                error)) {
    return false;
  }

  if (!absl::c_linear_search(payload_types, payload_type)) {
    RTC_LOG(LS_WARNING) << "Ignore rtpmap line that did not appear in the "
                           "<fmt> of the m-line: "
                        << line;
    return true;
  }
  const auto codec_params = split<SdpFields>(fields[1], '/');
  // <encoding name>/<clock rate>[/<encodingparameters>]
  // 2 mandatory fields
  if (codec_params.size() < 2 || codec_params.size() > 3) {
    return ParseFailed(line,
                       "Expected format \"<encoding name>/<clock rate>"
                       "[/<encodingparameters>]\".",
                       error);
  }
  const absl::string_view encoding_name = codec_params[0];
  int clock_rate = 0;
  if (!GetValueFromString(line, codec_params[1], &clock_rate, error)) {
    return false;
  }

  if (media_type == MediaType::VIDEO) {
    for (const Codec& existing_codec : media_desc->codecs()) {
      if (!existing_codec.name.empty() && payload_type == existing_codec.id &&
          (!absl::EqualsIgnoreCase(encoding_name, existing_codec.name) ||
           clock_rate != existing_codec.clockrate)) {
        StringBuilder description;
        description
            << "Duplicate "
            << (payload_type < kFirstDynamicPayloadTypeLowerRange
                    ? "statically assigned"
                    : "")
            << " payload type with conflicting codec name or clock rate.";
        return ParseFailed(line, description.Release(), error);
      }
    }
    UpdateCodec(payload_type, encoding_name, media_desc);
  } else if (media_type == MediaType::AUDIO) {
    // RFC 4566
    // For audio streams, <encoding parameters> indicates the number
    // of audio channels.  This parameter is OPTIONAL and may be
    // omitted if the number of channels is one, provided that no
    // additional parameters are needed.
    size_t channels = 1;
    if (codec_params.size() == 3) {
      if (!GetValueFromString(line, codec_params[2], &channels, error)) {
        return false;
      }
    }
    if (channels > kMaxNumberOfAudioChannels) {
      StringBuilder description;
      description << "At most " << kMaxNumberOfAudioChannels
                  << " channels are supported.";
      return ParseFailed(line, description.Release(), error);
    }

    for (const Codec& existing_codec : media_desc->codecs()) {
      // TODO: crbug.com/1338902 - re-add checks for clockrate and number of
      // channels.
      if (!existing_codec.name.empty() && payload_type == existing_codec.id &&
          (!absl::EqualsIgnoreCase(encoding_name, existing_codec.name))) {
        StringBuilder description;
        description
            << "Duplicate "
            << (payload_type < kFirstDynamicPayloadTypeLowerRange
                    ? "statically assigned"
                    : "")
            << " payload type with conflicting codec name or clock rate.";
        return ParseFailed(line, description.Release(), error);
      }
    }
    UpdateCodec(payload_type, encoding_name, clock_rate, 0, channels,
                media_desc);
  }
  return true;
}

// Adds or updates existing video codec corresponding to `payload_type`
// according to `packetization`.
void UpdateVideoCodecPacketization(MediaContentDescription* desc,
                                   PayloadType payload_type,
                                   absl::string_view packetization) {
  if (packetization != kPacketizationParamRaw) {
    // Ignore unsupported packetization attribute.
    return;
  }

  // Codec might already have been populated (from rtpmap).
  Codec& codec = GetOrAddCodecWithPayloadType(desc, payload_type);
  codec.packetization = std::string(packetization);
}

bool ParsePacketizationAttribute(absl::string_view line,
                                 const MediaType media_type,
                                 MediaContentDescription* media_desc,
                                 SdpParseError* error) {
  if (media_type != MediaType::VIDEO) {
    return true;
  }
  const auto packetization_fields =
      split<SdpFields>(line, kSdpDelimiterSpaceChar);
  if (packetization_fields.size() < 2) {
    return ParseFailedGetValue(line, kAttributePacketization, error);
  }
  absl::string_view payload_type_string;
  if (!GetValue(packetization_fields[0], kAttributePacketization,
                &payload_type_string, error)) {
    return false;
  }
  int pt_int;
  if (!GetPayloadTypeFromString(line, payload_type_string, &pt_int, error)) {
    return false;
  }
  PayloadType payload_type = PayloadType(pt_int);
  absl::string_view packetization = packetization_fields[1];
  UpdateVideoCodecPacketization(media_desc, payload_type, packetization);
  return true;
}

bool ParseRtcpFbAttribute(absl::string_view line,
                          const MediaType media_type,
                          MediaContentDescription* media_desc,
                          SdpParseError* error) {
  if (media_type != MediaType::AUDIO && media_type != MediaType::VIDEO) {
    return true;
  }
  const auto rtcp_fb_fields = split<SdpFields>(line, kSdpDelimiterSpaceChar);
  if (rtcp_fb_fields.size() < 2) {
    return ParseFailedGetValue(line, kAttributeRtcpFb, error);
  }
  absl::string_view payload_type_string;
  if (!GetValue(rtcp_fb_fields[0], kAttributeRtcpFb, &payload_type_string,
                error)) {
    return false;
  }
  PayloadType payload_type = kWildcardPayloadType;
  if (payload_type_string != "*") {
    int pt_int;
    if (!GetPayloadTypeFromString(line, payload_type_string, &pt_int, error)) {
      return false;
    }
    payload_type = PayloadType(pt_int);
  }
  absl::string_view id = rtcp_fb_fields[1];
  // The parameter is the concatenation of the remaining fields.
  std::string param;
  for (size_t i = 2; i < rtcp_fb_fields.size(); ++i) {
    param.append(rtcp_fb_fields[i]);
  }
  UpdateCodec(media_desc, payload_type, FeedbackParam(id, param));
  return true;
}

std::optional<Codec> PopWildcardCodec(std::vector<Codec>* codecs) {
  RTC_DCHECK(codecs);
  for (auto iter = codecs->begin(); iter != codecs->end(); ++iter) {
    if (iter->id == kWildcardPayloadType) {
      Codec wildcard_codec = std::move(*iter);
      codecs->erase(iter);
      return wildcard_codec;
    }
  }
  return std::nullopt;
}

void UpdateFromWildcardCodecs(MediaContentDescription* desc) {
  RTC_DCHECK(desc);
  std::vector<Codec>& codecs = desc->mutable_codecs();
  std::optional<Codec> wildcard_codec = PopWildcardCodec(&codecs);
  if (!wildcard_codec) {
    return;
  }
  for (auto& codec : codecs) {
    AddFeedbackParameters(wildcard_codec->feedback_params, &codec);
  }
}

// Update settings that are only valid if media-section-wide, but
// are represented as per-codecs settings.
// Note that this is done after UpdateFromWildcardCodecs, so settings
// that are set with wildcard will also be picked up here.
void UpdateFromMediaSectionWideSettings(MediaContentDescription* desc) {
  if (desc->codecs().empty()) {
    return;
  }
  bool all_codecs_have_ack_ccfb = true;
  for (const auto& codec : desc->codecs()) {
    if (!codec.feedback_params.Has({"ack", "ccfb"})) {
      all_codecs_have_ack_ccfb = false;
    }
  }
  if (all_codecs_have_ack_ccfb) {
    desc->set_rtcp_fb_ack_ccfb(true);
  }
}

void AddAudioAttribute(absl::string_view name,
                       absl::string_view value,
                       MediaContentDescription* desc) {
  RTC_DCHECK(desc);
  if (value.empty()) {
    return;
  }
  for (Codec& codec : desc->mutable_codecs()) {
    codec.SetParam(name, value);
  }
}

bool ParseContent(absl::string_view message,
                  const MediaType media_type,
                  int mline_index,
                  absl::string_view protocol,
                  const std::vector<int>& payload_types,
                  size_t* pos,
                  std::string* content_name,
                  bool* bundle_only,
                  int* msid_signaling,
                  MediaContentDescription* media_desc,
                  TransportDescription* transport,
                  IceCandidateCollection& candidates,
                  SdpParseError* error) {
  RTC_DCHECK(media_desc != nullptr);
  RTC_DCHECK(content_name != nullptr);
  RTC_DCHECK(transport != nullptr);

  if (media_type == MediaType::AUDIO) {
    MaybeCreateStaticPayloadAudioCodecs(payload_types, media_desc);
  }

  // The media level "ice-ufrag" and "ice-pwd".
  // The candidates before update the media level "ice-pwd" and "ice-ufrag".
  std::vector<Candidate> candidates_orig;
  std::string mline_id;
  // Tracks created out of the ssrc attributes.
  StreamParamsVec tracks;
  SsrcInfoVec ssrc_infos;
  SsrcGroupVec ssrc_groups;
  absl::string_view maxptime_as_string;
  absl::string_view ptime_as_string;
  std::vector<std::string> stream_ids;
  std::string track_id;
  SimulcastSdpSerializer deserializer;
  std::vector<RidDescription> rids;
  SimulcastDescription simulcast;

  // Loop until the next m line
  while (!IsLineType(message, kLineTypeMedia, *pos)) {
    std::optional<absl::string_view> line = GetLine(message, pos);
    if (!line.has_value()) {
      if (*pos >= message.size()) {
        break;  // Done parsing
      } else {
        return ParseFailed(message, *pos, "Invalid SDP line.", error);
      }
    }

    // RFC 4566
    // b=* (zero or more bandwidth information lines)
    if (IsLineType(*line, kLineTypeSessionBandwidth)) {
      absl::string_view bandwidth;
      absl::string_view bandwidth_type;
      if (!tokenize_first(line->substr(kLinePrefixLength),
                          kSdpDelimiterColonChar, &bandwidth_type,
                          &bandwidth)) {
        return ParseFailed(
            *line,
            "b= syntax error, does not match b=<modifier>:<bandwidth-value>.",
            error);
      }
      if (!(bandwidth_type == kApplicationSpecificBandwidth ||
            bandwidth_type == kTransportSpecificBandwidth)) {
        // Ignore unknown bandwidth types.
        continue;
      }
      int b = 0;
      if (!GetValueFromString(*line, bandwidth, &b, error)) {
        ReportSdpBandwidth(kSdpBandwidthParseFailure);
        return false;
      }
      if (b == 0) {
        ReportSdpBandwidth(kSdpBandwidthZero);
      } else if (b > 0 && b <= INT_MAX / 1000) {
        ReportSdpBandwidth(kSdpBandwidthSmall);
      } else if (b > INT_MAX / 1000) {
        ReportSdpBandwidth(kSdpBandwidthLarge);
      } else {
        ReportSdpBandwidth(kSdpBandwidthNegative);
      }
      if (b < 0) {
        return ParseFailed(
            *line,
            "b=" + std::string(bandwidth_type) + " value can't be negative.",
            error);
      }
      // Convert values. Prevent integer overflow.
      if (bandwidth_type == kApplicationSpecificBandwidth) {
        b = std::min(b, INT_MAX / 1000) * 1000;
      } else {
        b = std::min(b, INT_MAX);
      }
      media_desc->set_bandwidth(b);
      media_desc->set_bandwidth_type(std::string(bandwidth_type));
      continue;
    }

    // Parse the media level connection data.
    if (IsLineType(*line, kLineTypeConnection)) {
      SocketAddress addr;
      if (!ParseConnectionData(*line, &addr, error)) {
        return false;
      }
      media_desc->set_connection_address(addr);
      continue;
    }

    if (!IsLineType(*line, kLineTypeAttributes)) {
      // TODO(deadbeef): Handle other lines if needed.
      RTC_LOG(LS_VERBOSE) << "Ignored line: " << *line;
      continue;
    }

    absl::string_view attr = ExtractAttributeName(*line);

    // Handle attributes common to SCTP and RTP.
    if (attr == kAttributeMid) {
      // RFC 3388
      // mid-attribute      = "a=mid:" identification-tag
      // identification-tag = token
      // Use the mid identification-tag as the content name.
      if (!GetSingleTokenValue(*line, kAttributeMid, &mline_id, error)) {
        return false;
      }
      *content_name = mline_id;
    } else if (attr == kAttributeBundleOnly) {
      *bundle_only = true;
    } else if (attr == kAttributeCandidate) {
      RTCErrorOr<Candidate> candidate = Candidate::ParseCandidateString(*line);
      if (!candidate.ok()) {
        return ParseFailed(*line, candidate.error().message(), error);
      }
      // ParseCandidateString will parse non-standard ufrag and password
      // attributes, since it's used for candidate trickling, but we only want
      // to process the "a=ice-ufrag"/"a=ice-pwd" values in a session
      // description, so strip them off at this point.
      candidate.value().set_username("");
      candidate.value().set_password("");
      candidates_orig.push_back(candidate.MoveValue());
    } else if (attr == kAttributeIceUfrag) {
      if (!GetValue(*line, kAttributeIceUfrag, &transport->ice_ufrag, error)) {
        return false;
      }
    } else if (attr == kAttributeIcePwd) {
      if (!GetValue(*line, kAttributeIcePwd, &transport->ice_pwd, error)) {
        return false;
      }
    } else if (attr == kAttributeIceOption) {
      if (!ParseIceOptions(*line, &transport->transport_options, error)) {
        return false;
      }
    } else if (attr == kAttributeFmtp) {
      if (!ParseFmtpAttributes(*line, media_type, media_desc, error)) {
        return false;
      }
    } else if (attr == kAttributeFingerprint) {
      std::unique_ptr<SSLFingerprint> fingerprint;
      if (!ParseFingerprintAttribute(*line, &fingerprint, error)) {
        return false;
      }
      transport->identity_fingerprint = std::move(fingerprint);
    } else if (attr == kAttributeSetup) {
      if (!ParseDtlsSetup(*line, &(transport->connection_role), error)) {
        return false;
      }
    } else if (IsDtlsSctp(protocol) && media_type == MediaType::DATA) {
      //
      // SCTP specific attributes
      //
      if (attr == kAttributeSctpPort) {
        if (media_desc->as_sctp()->use_sctpmap()) {
          return ParseFailed(
              *line, "sctp-port attribute can't be used with sctpmap.", error);
        }
        int sctp_port;
        if (!ParseSctpPort(*line, &sctp_port, error)) {
          return false;
        }
        media_desc->as_sctp()->set_port(sctp_port);
      } else if (attr == kAttributeMaxMessageSize) {
        int max_message_size;
        if (!ParseSctpMaxMessageSize(*line, &max_message_size, error)) {
          return false;
        }
        media_desc->as_sctp()->set_max_message_size(max_message_size);
      } else if (attr == kAttributeSctpSnap) {
        // draft-hancke-tsvwg-snap
        std::vector<uint8_t> sctp_init;
        if (!ParseSctpInit(*line, &sctp_init, error)) {
          return false;
        }
        media_desc->as_sctp()->set_sctp_init(sctp_init);
      } else if (attr == kAttributeSctpmap) {
        // Ignore a=sctpmap: from early versions of draft-ietf-mmusic-sctp-sdp
        continue;
      }
    } else if (IsRtpProtocol(protocol)) {
      //
      // RTP specific attributes
      //
      if (attr == kAttributeRtcpMux) {
        media_desc->set_rtcp_mux(true);
      } else if (attr == kAttributeRtcpReducedSize) {
        media_desc->set_rtcp_reduced_size(true);
      } else if (attr == kAttributeRtcpXr) {
        // RFC 3611: a=rtcp-xr:<format>[ <format>]... Consume the rcvr-rtt
        // format (receiver reference time report); accept rcvr-rtt=all and
        // rcvr-rtt=sender. Match whole space-separated tokens, not a
        // substring, and require the '=' so partial matches are rejected.
        absl::string_view xr_value;
        if (GetValue(*line, kAttributeRtcpXr, &xr_value, error)) {
          for (absl::string_view format :
               split(xr_value, kSdpDelimiterSpaceChar)) {
            if (absl::StartsWith(format, kRtcpXrFormatRcvrRttPrefix)) {
              media_desc->set_receive_non_sender_rtt(true);
              break;
            }
          }
        }
      } else if (attr == kAttributeRtcpRemoteEstimate) {
        media_desc->set_remote_estimate(true);
      } else if (attr == kAttributeSframe) {
        media_desc->set_sframe_enabled(true);
      } else if (attr == kAttributeSsrcGroup) {
        if (!ParseSsrcGroupAttribute(*line, &ssrc_groups, error)) {
          return false;
        }
      } else if (attr == kAttributeSsrc) {
        if (!ParseSsrcAttribute(*line, &ssrc_infos, msid_signaling, error)) {
          return false;
        }
      } else if (attr == kAttributeRtpmap) {
        if (!ParseRtpmapAttribute(*line, media_type, payload_types, media_desc,
                                  error)) {
          return false;
        }
      } else if (attr == kCodecParamMaxPTime) {
        if (!GetValue(*line, kCodecParamMaxPTime, &maxptime_as_string, error)) {
          return false;
        }
      } else if (attr == kAttributePacketization) {
        if (!ParsePacketizationAttribute(*line, media_type, media_desc,
                                         error)) {
          return false;
        }
      } else if (attr == kAttributeRtcpFb) {
        if (!ParseRtcpFbAttribute(*line, media_type, media_desc, error)) {
          return false;
        }
      } else if (attr == kCodecParamPTime) {
        if (!GetValue(*line, kCodecParamPTime, &ptime_as_string, error)) {
          return false;
        }
      } else if (attr == kAttributeSendOnly) {
        media_desc->set_direction(RtpTransceiverDirection::kSendOnly);
      } else if (attr == kAttributeRecvOnly) {
        media_desc->set_direction(RtpTransceiverDirection::kRecvOnly);
      } else if (attr == kAttributeInactive) {
        media_desc->set_direction(RtpTransceiverDirection::kInactive);
      } else if (attr == kAttributeSendRecv) {
        media_desc->set_direction(RtpTransceiverDirection::kSendRecv);
      } else if (attr == kAttributeExtmapAllowMixed) {
        media_desc->set_extmap_allow_mixed_level(
            MediaContentDescription::AttributeLevel::kMedia);
      } else if (attr == kAttributeExtmap) {
        RtpExtension extmap;
        if (!ParseExtmap(*line, &extmap, error)) {
          return false;
        }
        media_desc->AddRtpHeaderExtension(extmap);
      } else if (attr == kAttributeXGoogleFlag) {
        // Experimental attribute.  Conference mode activates more aggressive
        // AEC and NS settings.
        // TODO(deadbeef): expose API to set these directly.
        absl::string_view flag_value;
        if (!GetValue(*line, kAttributeXGoogleFlag, &flag_value, error)) {
          return false;
        }
        if (flag_value == kValueConference)
          media_desc->set_conference_mode(true);
      } else if (attr == kAttributeMsid) {
        if (!ParseMsidAttribute(*line, &stream_ids, &track_id, error)) {
          return false;
        }
        *msid_signaling |= kMsidSignalingMediaSection;
      } else if (attr == kAttributeRid) {
        constexpr size_t kRidPrefixLength = kLinePrefixLength +
                                            kAttributeRid.size() +
                                            kSdpDelimiterColon.size();
        if (line->size() <= kRidPrefixLength) {
          RTC_LOG(LS_INFO) << "Ignoring empty RID attribute: " << *line;
          continue;
        }
        RTCErrorOr<RidDescription> error_or_rid_description =
            deserializer.DeserializeRidDescription(
                *media_desc, line->substr(kRidPrefixLength));

        // Malformed a=rid lines are discarded.
        if (!error_or_rid_description.ok()) {
          RTC_LOG(LS_INFO) << "Ignoring malformed RID line: '" << *line
                           << "'. Error: "
                           << error_or_rid_description.error().message();
          continue;
        }

        rids.push_back(error_or_rid_description.MoveValue());
      } else if (attr == kAttributeSimulcast) {
        constexpr size_t kSimulcastPrefixLength = kLinePrefixLength +
                                                  kAttributeSimulcast.size() +
                                                  kSdpDelimiterColon.size();
        if (line->size() <= kSimulcastPrefixLength) {
          return ParseFailed(*line, "Simulcast attribute is empty.", error);
        }

        if (!simulcast.empty()) {
          return ParseFailed(*line, "Multiple Simulcast attributes specified.",
                             error);
        }

        RTCErrorOr<SimulcastDescription> error_or_simulcast =
            deserializer.DeserializeSimulcastDescription(
                line->substr(kSimulcastPrefixLength));
        if (!error_or_simulcast.ok()) {
          return ParseFailed(*line,
                             std::string("Malformed simulcast line: ") +
                                 error_or_simulcast.error().message(),
                             error);
        }

        simulcast = error_or_simulcast.value();
      } else if (attr == kAttributeRtcp) {
        // Ignore and do not log a=rtcp line.
        // JSEP  section 5.8.2 (media section parsing) says to ignore it.
        continue;
      } else if (attr == kAttributeCryptex) {
        media_desc->set_cryptex_level(
            MediaContentDescription::AttributeLevel::kMedia);
      } else {
        // Unrecognized attribute in RTP protocol.
        RTC_LOG(LS_VERBOSE) << "Ignored line: " << *line;
        continue;
      }
    } else {
      // Only parse lines that we are interested of.
      RTC_LOG(LS_VERBOSE) << "Ignored line: " << *line;
      continue;
    }
  }

  // Remove duplicate rids.
  RemoveDuplicateRidDescriptions(payload_types, &rids);

  // If simulcast is specifed, split the rids into send and receive.
  // Rids that do not appear in simulcast attribute will be removed.
  std::vector<RidDescription> send_rids;
  std::vector<RidDescription> receive_rids;
  if (!simulcast.empty()) {
    // Verify that the rids in simulcast match rids in sdp.
    RemoveInvalidRidsFromSimulcast(rids, &simulcast);

    // Use simulcast description to figure out Send / Receive RIDs.
    auto find_rid = [&](absl::string_view rid_name) {
      return absl::c_find_if(rids, [rid_name](const RidDescription& rid) {
        return rid.rid == rid_name;
      });
    };

    for (const auto& layer : simulcast.send_layers().GetAllLayers()) {
      auto iter = find_rid(layer.rid);
      RTC_DCHECK(iter != rids.end());
      send_rids.push_back(*iter);
    }

    for (const auto& layer : simulcast.receive_layers().GetAllLayers()) {
      auto iter = find_rid(layer.rid);
      RTC_DCHECK(iter != rids.end());
      receive_rids.push_back(*iter);
    }

    media_desc->set_simulcast_description(simulcast);
  } else {
    // RID is specified in RFC 8851, which identifies a lot of usages.
    // We only support RFC 8853 usage of RID, not anything else.
    // Ignore all RID parameters when a=simulcast is missing.
    // In particular do NOT do send_rids = rids;
    RTC_LOG(LS_VERBOSE) << "Ignoring send_rids without simulcast";
  }

  media_desc->set_receive_rids(receive_rids);

  // Create tracks from the `ssrc_infos`.
  // If the stream_id/track_id for all SSRCS are identical, one StreamParams
  // will be created in CreateTracksFromSsrcInfos, containing all the SSRCs from
  // the m= section.
  if (!ssrc_infos.empty()) {
    CreateTracksFromSsrcInfos(ssrc_infos, stream_ids, track_id, &tracks,
                              *msid_signaling);
  } else if (media_type != MediaType::DATA &&
             (*msid_signaling & kMsidSignalingMediaSection)) {
    // If the stream_ids/track_id was signaled but SSRCs were unsignaled we
    // still create a track. This isn't done for data media types because
    // StreamParams aren't used for SCTP streams, and RTP data channels don't
    // support unsignaled SSRCs.
    // If track id was not specified, create a random one.
    if (track_id.empty()) {
      track_id = CreateRandomString(8);
    }
    CreateTrackWithNoSsrcs(stream_ids, track_id, send_rids, &tracks);
  }

  // Add the ssrc group to the track.
  for (const SsrcGroup& ssrc_group : ssrc_groups) {
    if (ssrc_group.ssrcs.empty()) {
      continue;
    }
    uint32_t ssrc = ssrc_group.ssrcs.front();
    for (StreamParams& track : tracks) {
      if (track.has_ssrc(ssrc)) {
        track.ssrc_groups.push_back(ssrc_group);
      }
    }
  }

  // Add the new tracks to the `media_desc`.
  for (StreamParams& track : tracks) {
    media_desc->AddStream(track);
  }

  UpdateFromWildcardCodecs(media_desc);
  UpdateFromMediaSectionWideSettings(media_desc);
  // Codec has not been populated correctly unless the name has been set. This
  // can happen if an SDP has an fmtp or rtcp-fb with a payload type but doesn't
  // have a corresponding "rtpmap" line. This should lead to a parse error.
  if (!absl::c_all_of(media_desc->codecs(),
                      [](const Codec& codec) { return !codec.name.empty(); })) {
    return ParseFailed("Failed to parse codecs correctly.", error);
  }
  if (media_type == MediaType::AUDIO) {
    AddAudioAttribute(kCodecParamMaxPTime, maxptime_as_string, media_desc);
    AddAudioAttribute(kCodecParamPTime, ptime_as_string, media_desc);
  }

  // RFC 5245
  // Update the candidates with the media level "ice-pwd" and "ice-ufrag".
  for (Candidate& candidate : candidates_orig) {
    RTC_DCHECK(candidate.username().empty() ||
               candidate.username() == transport->ice_ufrag);
    candidate.set_username(transport->ice_ufrag);
    RTC_DCHECK(candidate.password().empty());
    candidate.set_password(transport->ice_pwd);
    auto jsep_candidate =
        std::make_unique<IceCandidate>(mline_id, mline_index, candidate);
    if (!candidates.HasCandidate(jsep_candidate.get())) {
      candidates.add(std::move(jsep_candidate));
    }
  }

  return true;
}

std::unique_ptr<MediaContentDescription> ParseContentDescription(
    absl::string_view message,
    const MediaType media_type,
    int mline_index,
    absl::string_view protocol,
    const std::vector<int>& payload_types,
    size_t* pos,
    std::string* content_name,
    bool* bundle_only,
    int* msid_signaling,
    TransportDescription* transport,
    IceCandidateCollection& candidates,
    SdpParseError* error) {
  std::unique_ptr<MediaContentDescription> media_desc;
  if (media_type == MediaType::AUDIO) {
    media_desc = std::make_unique<AudioContentDescription>();
  } else if (media_type == MediaType::VIDEO) {
    media_desc = std::make_unique<VideoContentDescription>();
  } else {
    RTC_DCHECK_NOTREACHED();
    return nullptr;
  }

  media_desc->set_extmap_allow_mixed_level(
      MediaContentDescription::AttributeLevel::kNone);
  media_desc->set_cryptex_level(MediaContentDescription::AttributeLevel::kNone);
  if (!ParseContent(message, media_type, mline_index, protocol, payload_types,
                    pos, content_name, bundle_only, msid_signaling,
                    media_desc.get(), transport, candidates, error)) {
    return nullptr;
  }
  // Sort the codecs according to the m-line fmt list.
  // "size + 1" so that the lowest preference payload type has a preference of
  // 1, which is greater than the default (0) for payload types not in the fmt
  // list.
  constexpr int kMaxPayloadType = 127;
  std::array<int, kMaxPayloadType + 1> payload_type_preferences{};
  int preference = static_cast<int>(payload_types.size() + 1);
  for (int pt : payload_types) {
    RTC_DCHECK(IsValidRtpPayloadType(pt));
    payload_type_preferences[pt] = preference--;
  }
  std::vector<Codec>& codecs = media_desc->mutable_codecs();
  absl::c_sort(
      codecs, [&payload_type_preferences](const Codec& a, const Codec& b) {
        RTC_DCHECK(IsValidRtpPayloadType(a.id));
        RTC_DCHECK(IsValidRtpPayloadType(b.id));
        return payload_type_preferences[a.id] > payload_type_preferences[b.id];
      });
  // Backfill any default parameters.
  BackfillCodecParameters(codecs);

  return media_desc;
}

bool HasDuplicateMsidLines(SessionDescription* desc) {
  flat_set<std::pair<absl::string_view, absl::string_view>> seen_msids;
  for (const ContentInfo& content : desc->contents()) {
    for (const StreamParams& stream : content.media_description()->streams()) {
      if (!seen_msids.insert({stream.first_stream_id(), stream.id}).second) {
        return true;
      }
    }
  }
  return false;
}

bool ParseMediaDescription(
    absl::string_view message,
    const TransportDescription& session_td,
    const RtpHeaderExtensions& session_extmaps,
    size_t* pos,
    const SocketAddress& session_connection_addr,
    SessionDescription* desc,
    std::vector<IceCandidateCollection>& candidate_collections,
    SdpParseError* error) {
  RTC_DCHECK(desc != nullptr);
  int mline_index = -1;
  int msid_signaling = desc->msid_signaling();

  // Zero or more media descriptions
  // RFC 4566
  // m=<media> <port> <proto> <fmt>
  while (std::optional<absl::string_view> mline =
             GetLineWithType(message, pos, kLineTypeMedia)) {
    ++mline_index;

    std::vector<absl::string_view> fields =
        split(mline->substr(kLinePrefixLength), kSdpDelimiterSpaceChar);

    const size_t expected_min_fields = 4;
    if (fields.size() < expected_min_fields) {
      return ParseFailedExpectMinFieldNum(*mline, expected_min_fields, error);
    }
    bool port_rejected = false;
    // RFC 3264
    // To reject an offered stream, the port number in the corresponding stream
    // in the answer MUST be set to zero.
    if (fields[1] == kMediaPortRejected) {
      port_rejected = true;
    }

    int port = 0;
    if (!FromString<int>(fields[1], &port) || !IsValidPort(port)) {
      return ParseFailed(*mline, "The port number is invalid", error);
    }
    absl::string_view protocol = fields[2];

    // <fmt>
    std::vector<int> payload_types;
    if (IsRtpProtocol(protocol)) {
      for (size_t j = 3; j < fields.size(); ++j) {
        int pl = 0;
        if (!GetPayloadTypeFromString(*mline, fields[j], &pl, error)) {
          return false;
        }
        payload_types.push_back(pl);
      }
    }

    // Make a temporary TransportDescription based on `session_td`.
    // Some of this gets overwritten by ParseContent.
    TransportDescription transport(
        session_td.transport_options, session_td.ice_ufrag, session_td.ice_pwd,
        session_td.ice_mode, session_td.connection_role,
        session_td.identity_fingerprint.get());

    std::unique_ptr<MediaContentDescription> content;
    std::string content_name;
    bool bundle_only = false;
    int section_msid_signaling = kMsidSignalingNotUsed;
    IceCandidateCollection section_candidates;
    absl::string_view media_type = fields[0];
    if ((media_type == kSdpMediaTypeVideo ||
         media_type == kSdpMediaTypeAudio) &&
        !IsRtpProtocol(protocol)) {
      return ParseFailed(*mline, "Unsupported protocol for media type", error);
    }
    if (media_type == kSdpMediaTypeVideo) {
      content = ParseContentDescription(
          message, MediaType::VIDEO, mline_index, protocol, payload_types, pos,
          &content_name, &bundle_only, &section_msid_signaling, &transport,
          section_candidates, error);
    } else if (media_type == kSdpMediaTypeAudio) {
      content = ParseContentDescription(
          message, MediaType::AUDIO, mline_index, protocol, payload_types, pos,
          &content_name, &bundle_only, &section_msid_signaling, &transport,
          section_candidates, error);
    } else if (media_type == kSdpMediaTypeData && IsDtlsSctp(protocol)) {
      // The draft-03 format is:
      // m=application <port> DTLS/SCTP <sctp-port>...
      // use_sctpmap should be false.
      // The draft-26 format is:
      // m=application <port> UDP/DTLS/SCTP webrtc-datachannel
      // use_sctpmap should be false.
      auto data_desc = std::make_unique<SctpDataContentDescription>();
      // Default max message size is 64K
      // according to draft-ietf-mmusic-sctp-sdp-26
      data_desc->set_max_message_size(kDefaultSctpMaxMessageSize);
      int p;
      if (FromString(fields[3], &p)) {
        data_desc->set_port(p);
      } else if (fields[3] == kDefaultSctpmapProtocol) {
        data_desc->set_use_sctpmap(false);
      }
      if (!ParseContent(message, MediaType::DATA, mline_index, protocol,
                        payload_types, pos, &content_name, &bundle_only,
                        &section_msid_signaling, data_desc.get(), &transport,
                        section_candidates, error)) {
        return false;
      }
      data_desc->set_protocol(protocol);
      content = std::move(data_desc);
    } else {
      RTC_LOG(LS_WARNING) << "Unsupported media type: " << *mline;
      auto unsupported_desc =
          std::make_unique<UnsupportedContentDescription>(media_type);
      if (!ParseContent(message, MediaType::UNSUPPORTED, mline_index, protocol,
                        payload_types, pos, &content_name, &bundle_only,
                        &section_msid_signaling, unsupported_desc.get(),
                        &transport, section_candidates, error)) {
        return false;
      }
      unsupported_desc->set_protocol(protocol);
      content = std::move(unsupported_desc);
    }
    if (!content) {
      // ParseContentDescription returns NULL if failed.
      return false;
    }

    msid_signaling |= section_msid_signaling;

    bool content_rejected = false;
    // A port of 0 is not interpreted as a rejected m= section when it's
    // used along with a=bundle-only.
    if (bundle_only) {
      if (!port_rejected) {
        // Usage of bundle-only with a nonzero port is unspecified. So just
        // ignore bundle-only if we see this.
        bundle_only = false;
        RTC_LOG(LS_WARNING)
            << "a=bundle-only attribute observed with a nonzero "
               "port; this usage is unspecified so the attribute is being "
               "ignored.";
      }
    } else {
      // If not using bundle-only, interpret port 0 in the normal way; the m=
      // section is being rejected.
      content_rejected = port_rejected;
    }

    if (content->as_unsupported()) {
      content_rejected = true;
    } else if (IsRtpProtocol(protocol) && !content->as_sctp()) {
      content->set_protocol(std::string(protocol));
      // Set the extmap.
      if (!session_extmaps.empty() &&
          !content->rtp_header_extensions().empty()) {
        return ParseFailed("",
                           "The a=extmap MUST be either all session level or "
                           "all media level.",
                           error);
      }
      for (size_t i = 0; i < session_extmaps.size(); ++i) {
        content->AddRtpHeaderExtension(session_extmaps[i]);
      }
    } else if (content->as_sctp()) {
      // Do nothing, it's OK
    } else {
      RTC_LOG(LS_WARNING) << "Parse failed with unknown protocol " << protocol;
      return false;
    }

    // Use the session level connection address if the media level addresses are
    // not specified.
    SocketAddress address;
    address = content->connection_address().IsNil()
                  ? session_connection_addr
                  : content->connection_address();
    address.SetPort(port);
    content->set_connection_address(address);

    desc->AddContent(content_name,
                     IsDtlsSctp(protocol) ? MediaProtocolType::kSctp
                                          : MediaProtocolType::kRtp,
                     content_rejected, bundle_only, std::move(content));
    // Create TransportInfo with the media level "ice-pwd" and "ice-ufrag".
    desc->AddTransportInfo(TransportInfo(content_name, transport));
    candidate_collections.push_back(std::move(section_candidates));
  }
  // Apply whole-description sanity checks
  if (HasDuplicateMsidLines(desc)) {
    ParseFailed(message, *pos, "Duplicate a=msid lines detected", error);
    return false;
  }

  desc->set_msid_signaling(msid_signaling);

  size_t end_of_message = message.size();
  if (mline_index == -1 && *pos != end_of_message) {
    ParseFailed(message, *pos, "Expects m line.", error);
    return false;
  }
  return true;
}

bool IsMediaType(const MediaContentDescription* description, MediaType type) {
  return description && description->type() == type;
}

const ContentInfo* GetFirstMediaContent(const ContentInfos& contents,
                                        MediaType type) {
  for (const ContentInfo& content : contents) {
    if (IsMediaType(content.media_description(), type)) {
      return &content;
    }
  }
  return nullptr;
}

const ContentInfo* LegacyGetFirstAudioContent(const SessionDescription& sdesc) {
  return GetFirstMediaContent(sdesc.contents(), MediaType::AUDIO);
}

const ContentInfo* LegacyGetFirstVideoContent(const SessionDescription& sdesc) {
  return GetFirstMediaContent(sdesc.contents(), MediaType::VIDEO);
}

}  // namespace

std::string SdpSerialize(const SessionDescriptionInterface& jdesc) {
  const SessionDescription* desc = jdesc.description();
  if (!desc) {
    return "";
  }

  // Rough size estimates, used to avoid repeated reallocations of `message`.
  constexpr size_t kEstimatedSessionLevelSize = 256;
  constexpr size_t kEstimatedMediaSectionSize = 1024;
  std::string message;
  message.reserve(desc->contents().size() * kEstimatedMediaSectionSize +
                  kEstimatedSessionLevelSize);

  // Session Description.
  AddLine(kSessionVersion, &message);
  // Session Origin
  // RFC 4566
  // o=<username> <sess-id> <sess-version> <nettype> <addrtype>
  // <unicast-address>
  StringBuilder os;
  InitLine(kLineTypeOrigin, kSessionOriginUsername, &os);
  const std::string& session_id =
      jdesc.session_id().empty() ? kSessionOriginSessionId : jdesc.session_id();
  const std::string& session_version = jdesc.session_version().empty()
                                           ? kSessionOriginSessionVersion
                                           : jdesc.session_version();
  os << " " << session_id << " " << session_version << " "
     << kSessionOriginNettype << " " << kSessionOriginAddrtype << " "
     << kSessionOriginAddress;
  AddLine(os.str(), &message);
  AddLine(kSessionName, &message);

  // Time Description.
  AddLine(kTimeDescription, &message);

  // BUNDLE Groups
  std::vector<const ContentGroup*> groups =
      desc->GetGroupsByName(GROUP_TYPE_BUNDLE);
  for (const ContentGroup* group : groups) {
    RTC_DCHECK(group != nullptr);
    InitAttrLine(kAttributeGroup, &os);
    os << kSdpDelimiterColon << GROUP_TYPE_BUNDLE;
    for (const std::string& content_name : group->content_names()) {
      os << " " << content_name;
    }
    AddLine(os.str(), &message);
  }

  // Mixed one- and two-byte header extension.
  if (desc->extmap_allow_mixed()) {
    InitAttrLine(kAttributeExtmapAllowMixed, &os);
    AddLine(os.str(), &message);
  }
  if (desc->cryptex()) {
    InitAttrLine(kAttributeCryptex, &os);
    AddLine(os.str(), &message);
  }

  // MediaStream semantics.
  // TODO(bugs.webrtc.org/10421): Change to & kMsidSignalingSemantic
  // when we think it's safe to do so, so that we gradually fade out this old
  // line that was removed from the specification.
  if (desc->msid_signaling() != kMsidSignalingNotUsed) {
    InitAttrLine(kAttributeMsidSemantics, &os);
    os << kSdpDelimiterColon << " " << kMediaStreamSemantic;

    // TODO(bugs.webrtc.org/10421): this code only looks at the first
    // audio/video content. Fixing that might result in much larger SDP and the
    // msid-semantic line should eventually go away so this is not worth fixing.
    flat_set<std::string> media_stream_ids;
    const ContentInfo* audio_content = LegacyGetFirstAudioContent(*desc);
    if (audio_content)
      GetMediaStreamIds(audio_content, &media_stream_ids);

    const ContentInfo* video_content = LegacyGetFirstVideoContent(*desc);
    if (video_content)
      GetMediaStreamIds(video_content, &media_stream_ids);

    for (const std::string& id : media_stream_ids) {
      os << " " << id;
    }
    AddLine(os.str(), &message);
  }

  // a=ice-lite
  //
  // TODO(deadbeef): It's weird that we need to iterate TransportInfos for
  // this, when it's a session-level attribute. It really should be moved to a
  // session-level structure like SessionDescription.
  for (const TransportInfo& transport : desc->transport_infos()) {
    if (transport.description.ice_mode == ICEMODE_LITE) {
      InitAttrLine(kAttributeIceLite, &os);
      AddLine(os.str(), &message);
      break;
    }
  }

  // Preserve the order of the media contents.
  //
  // `transport_infos` normally has one entry per content, in the same order
  // as the contents: the SDP parser and MediaSessionDescriptionFactory add a
  // TransportInfo along with each content. So the entry at the content's
  // index is checked first. This avoids a linear search by name for each
  // m= section, which would be quadratic in the number of m= sections. If
  // there is no entry at that index or it belongs to another content, e.g.
  // because TransportInfos were removed or reordered through the
  // SessionDescription API, the search by name is the fallback. Both find
  // the same TransportInfo as long as no two TransportInfos have the same
  // content name, which holds when the MIDs are unique
  // (SetLocalDescription() and SetRemoteDescription() reject duplicates).
  const TransportInfos& transport_infos = desc->transport_infos();
  size_t mline_index = 0;
  for (const ContentInfo& content : desc->contents()) {
    const TransportInfo* transport_info =
        (mline_index < transport_infos.size() &&
         transport_infos[mline_index].content_name == content.mid())
            ? &transport_infos[mline_index]
            : desc->GetTransportInfoByName(content.mid());
    BuildMediaDescription(
        &content, transport_info, content.media_description()->type(),
        *jdesc.candidates(mline_index), desc->msid_signaling(),
        jdesc.encoding_options().use_wildcard, &os, &message);
    ++mline_index;
  }
  return message;
}

std::unique_ptr<SessionDescriptionInterface> SdpDeserialize(
    SdpType sdp_type,
    absl::string_view message,
    SdpParseError* absl_nullable error) {
  std::string session_id;
  std::string session_version;
  TransportDescription session_td("", "");
  RtpHeaderExtensions session_extmaps;
  SocketAddress session_connection_addr;
  auto desc = std::make_unique<SessionDescription>();
  size_t current_pos = 0;

  // Session Description
  if (!ParseSessionDescription(message, &current_pos, &session_id,
                               &session_version, &session_td, &session_extmaps,
                               &session_connection_addr, desc.get(), error)) {
    return nullptr;
  }

  // Media Description
  std::vector<IceCandidateCollection> candidates;
  if (!ParseMediaDescription(message, session_td, session_extmaps, &current_pos,
                             session_connection_addr, desc.get(), candidates,
                             error)) {
    return nullptr;
  }

  return SessionDescriptionInterface::Create(sdp_type, std::move(desc),
                                             session_id, session_version,
                                             std::move(candidates));
}

}  // namespace webrtc
