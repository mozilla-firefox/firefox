/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/jsep.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/sequence_checker.h"
#include "api/webrtc_sdp.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/transport_info.h"
#include "pc/session_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/socket_address.h"

namespace webrtc {
namespace {
constexpr char kDummyAddress[] = "0.0.0.0";
constexpr int kDummyPort = 9;

// Update the connection address for the MediaContentDescription based on the
// candidates.
void UpdateConnectionAddress(const IceCandidateCollection& candidate_collection,
                             MediaContentDescription* media_desc) {
  int port = kDummyPort;
  std::string ip = kDummyAddress;
  std::string hostname;
  int current_preference = 0;  // Start with lowest preference.
  int current_family = AF_UNSPEC;
  for (size_t i = 0; i < candidate_collection.count(); ++i) {
    const IceCandidate* jsep_candidate = candidate_collection.at(i);
    if (jsep_candidate->candidate().component() !=
        ICE_CANDIDATE_COMPONENT_RTP) {
      continue;
    }
    // Default destination should be UDP only.
    if (jsep_candidate->candidate().protocol() != UDP_PROTOCOL_NAME) {
      continue;
    }
    const int preference = jsep_candidate->candidate().type_preference();
    const int family = jsep_candidate->candidate().address().ipaddr().family();
    // See if this candidate is more preferable then the current one if it's the
    // same family. Or if the current family is IPv4 already so we could safely
    // ignore all IPv6 ones. WebRTC bug 4269.
    // http://code.google.com/p/webrtc/issues/detail?id=4269
    if ((preference <= current_preference && current_family == family) ||
        (current_family == AF_INET && family == AF_INET6)) {
      continue;
    }
    current_preference = preference;
    current_family = family;
    const SocketAddress& candidate_addr = jsep_candidate->candidate().address();
    port = candidate_addr.port();
    ip = candidate_addr.ipaddr().ToString();
    hostname = candidate_addr.hostname();
  }
  SocketAddress connection_addr(ip, port);
  if (IPIsUnspec(connection_addr.ipaddr()) && !hostname.empty()) {
    // When a hostname candidate becomes the (default) connection address,
    // we use the dummy address 0.0.0.0 and port 9 in the c= and the m= lines.
    //
    // We have observed in deployment that with a FQDN in a c= line, SDP parsing
    // could fail in other JSEP implementations. We note that the wildcard
    // addresses (0.0.0.0 or ::) with port 9 are given the exception as the
    // connection address that will not result in an ICE mismatch
    // (draft-ietf-mmusic-ice-sip-sdp). Also, 0.0.0.0 or :: can be used as the
    // connection address in the initial offer or answer with trickle ICE
    // if the offerer or answerer does not want to include the host IP address
    // (draft-ietf-mmusic-trickle-ice-sip), and in particular 0.0.0.0 has been
    // widely deployed for this use without outstanding compatibility issues.
    // Combining the above considerations, we use 0.0.0.0 with port 9 to
    // populate the c= and the m= lines. See `BuildMediaDescription` in
    // webrtc_sdp.cc for the SDP generation with
    // `media_desc->connection_address()`.
    connection_addr = SocketAddress(kDummyAddress, kDummyPort);
  }
  media_desc->set_connection_address(connection_addr);
}

std::vector<IceCandidateCollection> CloneCandidateCollection(
    const std::vector<IceCandidateCollection>& original) {
  std::vector<IceCandidateCollection> ret;
  ret.reserve(original.size());
  for (const auto& collection : original) {
    ret.push_back(collection.Clone());
  }
  return ret;
}
}  // namespace

const char SessionDescriptionInterface::kOffer[] = "offer";
const char SessionDescriptionInterface::kPrAnswer[] = "pranswer";
const char SessionDescriptionInterface::kAnswer[] = "answer";
const char SessionDescriptionInterface::kRollback[] = "rollback";

const char* SdpTypeToString(SdpType type) {
  switch (type) {
    case SdpType::kOffer:
      return SessionDescriptionInterface::kOffer;
    case SdpType::kPrAnswer:
      return SessionDescriptionInterface::kPrAnswer;
    case SdpType::kAnswer:
      return SessionDescriptionInterface::kAnswer;
    case SdpType::kRollback:
      return SessionDescriptionInterface::kRollback;
  }
  return "";
}

std::optional<SdpType> SdpTypeFromString(const std::string& type_str) {
  if (type_str == SessionDescriptionInterface::kOffer) {
    return SdpType::kOffer;
  } else if (type_str == SessionDescriptionInterface::kPrAnswer) {
    return SdpType::kPrAnswer;
  } else if (type_str == SessionDescriptionInterface::kAnswer) {
    return SdpType::kAnswer;
  } else if (type_str == SessionDescriptionInterface::kRollback) {
    return SdpType::kRollback;
  } else {
    return std::nullopt;
  }
}

std::unique_ptr<IceCandidate> CreateIceCandidate(absl::string_view sdp_mid,
                                                 int sdp_mline_index,
                                                 const Candidate& candidate) {
  return std::make_unique<IceCandidate>(sdp_mid, sdp_mline_index, candidate);
}

IceCandidate* CreateIceCandidate(absl::string_view sdp_mid,
                                 int sdp_mline_index,
                                 const std::string& sdp,
                                 SdpParseError* error) {
  return IceCandidate::Create(sdp_mid, sdp_mline_index, sdp, error).release();
}

std::unique_ptr<SessionDescriptionInterface> CreateSessionDescription(
    SdpType type,
    absl::string_view sdp) {
  return CreateSessionDescription(type, sdp, nullptr);
}

std::unique_ptr<SessionDescriptionInterface> CreateSessionDescription(
    SdpType type,
    absl::string_view sdp,
    SdpParseError* error_out) {
  if (type == SdpType::kRollback) {
    return CreateRollbackSessionDescription();
  }
  return SdpDeserialize(type, sdp, error_out);
}

std::unique_ptr<SessionDescriptionInterface> CreateSessionDescription(
    SdpType type,
    absl::string_view session_id,
    absl::string_view session_version,
    std::unique_ptr<SessionDescription> description) {
  return SessionDescriptionInterface::Create(type, std::move(description),
                                             session_id, session_version);
}

std::unique_ptr<SessionDescriptionInterface> CreateRollbackSessionDescription(
    absl::string_view session_id,
    absl::string_view session_version) {
  return SessionDescriptionInterface::Create(
      SdpType::kRollback, /*description=*/nullptr, session_id, session_version);
}

// static
std::unique_ptr<SessionDescriptionInterface>
SessionDescriptionInterface::Create(
    SdpType type,
    std::unique_ptr<SessionDescription> description,
    absl::string_view id,
    absl::string_view version,
    std::vector<IceCandidateCollection> candidates,
    EncodingOptions encoding_options) {
  if (!description && type != SdpType::kRollback)
    return nullptr;
  return absl::WrapUnique(
      new SessionDescriptionInterface(type, std::move(description), id, version,
                                      std::move(candidates), encoding_options));
}

SessionDescriptionInterface::~SessionDescriptionInterface() = default;

void SessionDescriptionInterface::RelinquishThreadOwnership() {
  // Ideally we should require that the method can only be called from the
  // thread that the sequence checker is currently attached to. However that's
  // not compatible with some cases outside of webrtc where initializations
  // happens on one thread and then the object is moved to a second thread (e.g.
  // signaling) where a call is made into webrtc. At that point we'd hit a
  // dcheck like this in webrtc: RTC_DCHECK_RUN_ON(&sequence_checker_);
  sequence_checker_.Detach();
  // Tie the checker to the current thread, which permits iterating
  // `candidate_collection_`
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  for (IceCandidateCollection& collection : candidate_collection_) {
    collection.RelinquishThreadOwnership();
  }
  sequence_checker_.Detach();  // Unties the checker from the current thread.
}

SessionDescriptionInterface::SessionDescriptionInterface(
    SdpType type,
    std::unique_ptr<SessionDescription> desc,
    absl::string_view id,
    absl::string_view version,
    std::vector<IceCandidateCollection> candidates,
    EncodingOptions encoding_options)
    : sdp_type_(type),
      id_(id),
      version_(version),
      description_(std::move(desc)),
      candidate_collection_(std::move(candidates)),
      encoding_options_(encoding_options) {
  RTC_DCHECK(description() || type == SdpType::kRollback);
  RTC_DCHECK(candidate_collection_.empty() ||
             candidate_collection_.size() == number_of_mediasections());
  candidate_collection_.resize(number_of_mediasections());
  for (size_t i = 0; i < candidate_collection_.size(); ++i) {
    if (!candidate_collection_[i].empty()) {
      UpdateConnectionAddress(candidate_collection_[i],
                              description_->contents()[i].media_description());
    }
    // The calls above attach the collection's sequence checker to the current
    // thread. Detach it so that, like `sequence_checker_`, the description
    // isn't tied to the thread that constructs it. E.g. Chromium parses SDP on
    // the main thread and then uses the description on the signaling thread.
    candidate_collection_[i].RelinquishThreadOwnership();
  }
}

size_t SessionDescriptionInterface::number_of_mediasections() const {
  return description_ ? description_->contents().size() : 0u;
}

std::unique_ptr<SessionDescriptionInterface>
SessionDescriptionInterface::Clone() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return SessionDescriptionInterface::Create(
      sdp_type_, description() ? description()->Clone() : nullptr, id(),
      version(), CloneCandidateCollection(candidate_collection_),
      encoding_options_);
}

bool SessionDescriptionInterface::AddCandidate(const IceCandidate* candidate) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (!candidate)
    return false;
  size_t index = 0;
  if (!GetMediasectionIndex(candidate, &index)) {
    return false;
  }
  ContentInfo& content = description()->contents()[index];
  const TransportInfo* transport_info =
      description()->GetTransportInfoByName(content.mid());
  if (!transport_info) {
    return false;
  }

  Candidate updated_candidate = candidate->candidate();
  if (updated_candidate.username().empty()) {
    updated_candidate.set_username(transport_info->description.ice_ufrag);
  }
  if (updated_candidate.password().empty()) {
    updated_candidate.set_password(transport_info->description.ice_pwd);
  }

  // Use `content.mid()` as the mid for the updated candidate. The
  // `candidate->sdp_mid()` property *should* be the same. However, in some
  // cases specifying an empty mid but a valid index is a way to add a candidate
  // without knowing (or caring about) the mid. This is done in several tests.
  RTC_DCHECK(candidate->sdp_mid().empty() ||
             candidate->sdp_mid() == content.mid())
      << "sdp_mid='" << candidate->sdp_mid() << "' content.mid()='"
      << content.mid() << "'";
  auto updated_candidate_wrapper = std::make_unique<IceCandidate>(
      content.mid(), static_cast<int>(index), updated_candidate);
  IceCandidateCollection& candidates = candidate_collection_[index];
  if (!candidates.HasCandidate(updated_candidate_wrapper.get())) {
    candidates.add(std::move(updated_candidate_wrapper));
    UpdateConnectionAddress(candidates, content.media_description());
  }

  return true;
}

bool SessionDescriptionInterface::RemoveCandidate(
    const IceCandidate* candidate) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  size_t index = 0u;
  if (!GetMediasectionIndex(candidate, &index)) {
    return false;
  }
  IceCandidateCollection& candidates = candidate_collection_[index];
  if (!candidates.remove(candidate)) {
    return false;
  }
  UpdateConnectionAddress(candidates,
                          description()->contents()[index].media_description());
  return true;
}

const IceCandidateCollection* SessionDescriptionInterface::candidates(
    size_t mediasection_index) const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (mediasection_index >= candidate_collection_.size())
    return nullptr;
  return &candidate_collection_[mediasection_index];
}

std::string SessionDescriptionInterface::ToString() const {
  return SdpSerialize(*this);
}

bool SessionDescriptionInterface::IsValidMLineIndex(int index) const {
  RTC_DCHECK(description());
  return index >= 0 &&
         index < static_cast<int>(description()->contents().size());
}

bool SessionDescriptionInterface::GetMediasectionIndex(
    const IceCandidate* candidate,
    size_t* index) const {
  if (!candidate || !index || !description()) {
    return false;
  }

  auto mid = candidate->sdp_mid();
  if (!mid.empty()) {
    *index = GetMediasectionIndex(mid);
  } else {
    // An sdp_mline_index of -1 will be treated as invalid.
    *index = static_cast<size_t>(candidate->sdp_mline_index());
  }
  return IsValidMLineIndex(*index);
}

int SessionDescriptionInterface::GetMediasectionIndex(
    absl::string_view mid) const {
  const auto& contents = description()->contents();
  auto it =
      std::find_if(contents.begin(), contents.end(),
                   [&](const auto& content) { return mid == content.mid(); });
  return it == contents.end() ? -1 : std::distance(contents.begin(), it);
}

}  // namespace webrtc
