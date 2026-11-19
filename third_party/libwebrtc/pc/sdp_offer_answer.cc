/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/sdp_offer_answer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "api/audio_options.h"
#include "api/candidate.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/payload_type.h"
#include "api/peer_connection_interface.h"
#include "api/peer_connection_tracer_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/uma_metrics.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video/video_codec_constants.h"
#include "media/base/codec.h"
#include "media/base/codec_comparators.h"
#include "media/base/media_channel.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/rid_description.h"
#include "media/base/stream_params.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/p2p_transport_channel.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_description_factory.h"
#include "p2p/base/transport_info.h"
#include "pc/channel_interface.h"
#include "pc/codec_vendor.h"
#include "pc/connection_context.h"
#include "pc/dtls_transport.h"
#include "pc/jsep_transport_controller.h"
#include "pc/legacy_stats_collector.h"
#include "pc/media_options.h"
#include "pc/media_protocol_names.h"
#include "pc/media_session.h"
#include "pc/media_stream.h"
#include "pc/media_stream_observer.h"
#include "pc/media_stream_proxy.h"
#include "pc/peer_connection_internal.h"
#include "pc/peer_connection_message_handler.h"
#include "pc/rtp_media_utils.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transceiver.h"
#include "pc/rtp_transmission_manager.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/sdp_munging_detector.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "pc/stream_collection.h"
#include "pc/transceiver_list.h"
#include "pc/usage_pattern.h"
#include "pc/webrtc_session_description_factory.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/logging.h"
#include "rtc_base/operations_chain.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/rtc_certificate_generator.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#if !RTC_DCHECK_IS_ON
#include "rtc_base/system/unused.h"
#endif
#include "rtc_base/trace_event.h"
#include "rtc_base/weak_ptr.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {
namespace {

void MaybeHandleLocallyRejectedTransceiver(
    ContentSource source,
    const SessionDescriptionInterface& new_session,
    const ContentInfo& new_content,
    scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>> transceiver,
    ScopedOperationsBatcher& worker_tasks) {
  if (source != ContentSource::CS_LOCAL || !new_content.rejected) {
    return;
  }
  if (new_session.GetType() == SdpType::kOffer) {
    // If the RtpTransceiver API was used, it would already have made the
    // transceiver stopping. But if the rejection was caused by SDP
    // munging then we need to ensure the transceiver is stopping here.
    if (!transceiver->internal()->stopping()) {
      worker_tasks.AddWithFinalizer(
          transceiver->internal()->StopStandardAsync());
    }
    RTC_DCHECK(transceiver->internal()->stopping());
  } else {
    RTC_DCHECK(new_session.GetType() == SdpType::kAnswer ||
               new_session.GetType() == SdpType::kPrAnswer);
    // When RtpTransceiver API is used, rejection happens in the offer and
    // the transceiver will already be stopped at local answer time
    // (calling stop between SRD(offer) and SLD(answer) would not reject
    // the content in the answer - instead this would trigger a follow-up
    // O/A exchange). So if the content was rejected but the transceiver
    // is not already stopped, SDP munging has happened and we need to
    // ensure the transceiver is stopped.
    if (!transceiver->internal()->stopped()) {
      worker_tasks.Add(transceiver->internal()->GetStopTransceiverProcedure());
    }
    RTC_DCHECK(transceiver->internal()->stopped());
  }
}

struct DtlsTransportAndName {
  scoped_refptr<DtlsTransport> transport;
  std::optional<std::string> transport_name;
};

typedef PeerConnectionInterface::RTCOfferAnswerOptions RTCOfferAnswerOptions;

// Error messages
const char kInvalidSdp[] = "Invalid session description.";
const char kInvalidCandidates[] = "Description contains invalid candidates.";
const char kBundleWithoutRtcpMux[] =
    "rtcp-mux must be enabled when BUNDLE "
    "is enabled.";
const char kMlineMismatchInAnswer[] =
    "The order of m-lines in answer doesn't match order in offer. Rejecting "
    "answer.";
const char kMlineMismatchInSubsequentOffer[] =
    "The order of m-lines in subsequent offer doesn't match order from "
    "previous offer/answer.";
const char kSdpWithoutIceUfragPwd[] =
    "Called with SDP without ice-ufrag and ice-pwd.";
const char kSdpWithoutDtlsFingerprint[] =
    "Called with SDP without DTLS fingerprint.";
const char kSdpWithoutCrypto[] = "Called with SDP without crypto setup.";
const char kSframeNotInOffer[] =
    "Remote answer has a=sframe for a media section but the corresponding "
    "offer did not include a=sframe.";

const char kSessionError[] = "Session error code: ";
const char kSessionErrorDesc[] = "Session error description: ";

// The length of RTCP CNAMEs.
const int kRtcpCnameLength = 16;

// The maximum length of the MID attribute.
constexpr size_t kMidMaxSize = 16;

const char kDefaultStreamId[] = "default";
// NOTE: Duplicated in peer_connection.cc:
const char kDefaultAudioSenderId[] = "defaulta0";
const char kDefaultVideoSenderId[] = "defaultv0";

flat_map<std::string, const ContentGroup*> GetBundleGroupsByMid(
    const SessionDescription* desc) {
  std::vector<const ContentGroup*> bundle_groups =
      desc->GetGroupsByName(GROUP_TYPE_BUNDLE);
  flat_map<std::string, const ContentGroup*> bundle_groups_by_mid;
  for (const ContentGroup* bundle_group : bundle_groups) {
    for (const std::string& content_name : bundle_group->content_names()) {
      bundle_groups_by_mid[content_name] = bundle_group;
    }
  }
  return bundle_groups_by_mid;
}

// Helper function to look up DTLS transports for all transceivers. Uses the
// transport states that are cached on the signaling thread, so no blocking
// call to the network thread is needed.
flat_map<std::string, DtlsTransportAndName> GetDtlsTransports(
    const TransceiverList& transceivers,
    const JsepTransportController* absl_nonnull transport_controller) {
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  const auto& transceiver_list = transceivers.ListRef();
  std::vector<std::pair<std::string, DtlsTransportAndName>> entries;
  entries.reserve(transceiver_list.size());
  for (const auto& transceiver : transceiver_list) {
    const std::optional<std::string> mid = transceiver->internal()->mid();
    if (!mid) {
      continue;
    }
    std::optional<JsepTransportController::TransportState> state =
        transport_controller->GetTransportState(*mid);
    entries.emplace_back(
        *mid, state ? DtlsTransportAndName{std::move(state->dtls_transport),
                                           std::move(state->transport_name)}
                    : DtlsTransportAndName{});
  }
  return flat_map<std::string, DtlsTransportAndName>(std::move(entries));
}

// Returns true if `new_desc` requests an ICE restart (i.e., new ufrag/pwd).
bool CheckForRemoteIceRestart(const SessionDescriptionInterface* old_desc,
                              const SessionDescriptionInterface* new_desc,
                              absl::string_view content_name) {
  if (!old_desc) {
    return false;
  }
  const SessionDescription* new_sd = new_desc->description();
  const SessionDescription* old_sd = old_desc->description();
  const ContentInfo* cinfo = new_sd->GetContentByName(content_name);
  if (!cinfo || cinfo->rejected) {
    return false;
  }
  // If the content isn't rejected, check if ufrag and password has changed.
  const TransportDescription* new_transport_desc =
      new_sd->GetTransportDescriptionByName(content_name);
  const TransportDescription* old_transport_desc =
      old_sd->GetTransportDescriptionByName(content_name);
  if (!new_transport_desc || !old_transport_desc) {
    // No transport description exists. This is not an ICE restart.
    return false;
  }
  if (IceCredentialsChanged(
          old_transport_desc->ice_ufrag, old_transport_desc->ice_pwd,
          new_transport_desc->ice_ufrag, new_transport_desc->ice_pwd)) {
    RTC_LOG(LS_INFO) << "Remote peer requests ICE restart for " << content_name
                     << ".";
    return true;
  }
  return false;
}

// Generates a string error message for SetLocalDescription/SetRemoteDescription
// from an RTCError.
std::string GetSetDescriptionErrorMessage(ContentSource source,
                                          SdpType type,
                                          const RTCError& error) {
  StringBuilder oss;
  oss << "Failed to set " << (source == CS_LOCAL ? "local" : "remote") << " "
      << SdpTypeToString(type) << " sdp: ";
  RTC_DCHECK(!absl::StartsWith(error.message(), oss.str())) << error.message();
  oss << error.message();
  return oss.Release();
}

std::string GetStreamIdsString(std::span<const std::string> stream_ids) {
  std::string output = "streams=[";
  const char* separator = "";
  for (const auto& stream_id : stream_ids) {
    output.append(separator).append(stream_id);
    separator = ", ";
  }
  output.append("]");
  return output;
}

const ContentInfo* FindTransceiverMSection(
    RtpTransceiver* transceiver,
    const SessionDescriptionInterface* session_description) {
  return transceiver->mid()
             ? session_description->description()->GetContentByName(
                   *transceiver->mid())
             : nullptr;
}

// If the direction is "recvonly" or "inactive", treat the description
// as containing no streams.
// See: https://code.google.com/p/webrtc/issues/detail?id=5054
std::vector<StreamParams> GetActiveStreams(
    const MediaContentDescription* desc) {
  return RtpTransceiverDirectionHasSend(desc->direction())
             ? desc->streams()
             : std::vector<StreamParams>();
}

// Logic to decide if an m= section can be recycled. This means that the new
// m= section is not rejected, but the old local or remote m= section is
// rejected. `old_content_one` and `old_content_two` refer to the m= section
// of the old remote and old local descriptions in no particular order.
// We need to check both the old local and remote because either
// could be the most current from the latest negotation.
bool IsMediaSectionBeingRecycled(SdpType type,
                                 const ContentInfo& content,
                                 const ContentInfo* old_content_one,
                                 const ContentInfo* old_content_two) {
  return type == SdpType::kOffer && !content.rejected &&
         ((old_content_one && old_content_one->rejected) ||
          (old_content_two && old_content_two->rejected));
}

// Verify that the order of media sections in `new_desc` matches
// `current_desc`. The number of m= sections in `new_desc` should be no
// less than `current_desc`. In the case of checking an answer's
// `new_desc`, the `current_desc` is the last offer that was set as the
// local or remote. In the case of checking an offer's `new_desc` we
// check against the local and remote descriptions stored from the last
// negotiation, because either of these could be the most up to date for
// possible rejected m sections. These are the `current_desc` and
// `secondary_current_desc`.
bool MediaSectionsInSameOrder(const SessionDescription& current_desc,
                              const SessionDescription* secondary_current_desc,
                              const SessionDescription& new_desc,
                              const SdpType type) {
  if (current_desc.contents().size() > new_desc.contents().size()) {
    return false;
  }

  for (size_t i = 0; i < current_desc.contents().size(); ++i) {
    const ContentInfo* secondary_content_info = nullptr;
    if (secondary_current_desc &&
        i < secondary_current_desc->contents().size()) {
      secondary_content_info = &secondary_current_desc->contents()[i];
    }
    if (IsMediaSectionBeingRecycled(type, new_desc.contents()[i],
                                    &current_desc.contents()[i],
                                    secondary_content_info)) {
      // For new offer descriptions, if the media section can be recycled, it's
      // valid for the MID and media type to change.
      continue;
    }
    if (new_desc.contents()[i].mid() != current_desc.contents()[i].mid()) {
      return false;
    }
    const MediaContentDescription* new_desc_mdesc =
        new_desc.contents()[i].media_description();
    const MediaContentDescription* current_desc_mdesc =
        current_desc.contents()[i].media_description();
    if (new_desc_mdesc->type() != current_desc_mdesc->type()) {
      return false;
    }
  }
  return true;
}

bool MediaSectionsHaveSameCount(const SessionDescription& desc1,
                                const SessionDescription& desc2) {
  return desc1.contents().size() == desc2.contents().size();
}

// Checks that the remote answer follows the rules from
// https://datatracker.ietf.org/doc/html/rfc3264#section-6.1
RTCError VerifyDirectionsInAnswer(const SessionDescription* local_offer,
                                  const SessionDescription* remote_answer) {
  RTC_DCHECK(local_offer);
  RTC_DCHECK(remote_answer);

  const ContentInfos& local_contents = local_offer->contents();
  const ContentInfos& remote_contents = remote_answer->contents();
  RTC_DCHECK(local_contents.size() == remote_contents.size());

  for (size_t i = 0; i < local_contents.size(); i++) {
    if (remote_contents[i].rejected) {
      continue;
    }
    RtpTransceiverDirection local_direction =
        local_contents[i].media_description()->direction();
    RtpTransceiverDirection remote_direction =
        remote_contents[i].media_description()->direction();

    if (!RtpTransceiverDirectionHasRecv(local_direction) &&
        RtpTransceiverDirectionHasSend(remote_direction)) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "Incompatible receive direction");
    }
    if (!RtpTransceiverDirectionHasSend(local_direction) &&
        RtpTransceiverDirectionHasRecv(remote_direction)) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "Incompatible send direction");
    }
  }
  return RTCError::OK();
}

// Checks that no answer m-section introduces a=sframe that was not present in
// the corresponding offer m-section.
RTCError VerifySframeInAnswer(const SessionDescription* local_offer,
                              const SessionDescription* remote_answer) {
  RTC_DCHECK(local_offer);
  RTC_DCHECK(remote_answer);

  const ContentInfos& offer_contents = local_offer->contents();
  const ContentInfos& answer_contents = remote_answer->contents();

  for (size_t i = 0; i < answer_contents.size() && i < offer_contents.size();
       ++i) {
    if (answer_contents[i].rejected) {
      continue;
    }
    const MediaContentDescription* answer_media =
        answer_contents[i].media_description();
    const MediaContentDescription* offer_media =
        offer_contents[i].media_description();
    if (answer_media->sframe_enabled() && !offer_media->sframe_enabled()) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << kSframeNotInOffer);
    }
  }
  return RTCError::OK();
}

// Checks that each non-rejected content has a DTLS
// fingerprint, unless it's in a BUNDLE group, in which case only the
// BUNDLE-tag section (first media section/description in the BUNDLE group)
// needs a ufrag and pwd. Mismatches, such as replying with a DTLS fingerprint
// to SDES keys, will be caught in JsepTransport negotiation, and backstopped
// by Channel's `srtp_required` check.
RTCError VerifyCrypto(
    const SessionDescription* desc,
    bool dtls_enabled,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  for (const ContentInfo& content_info : desc->contents()) {
    if (content_info.rejected) {
      continue;
    }
    const auto mid = content_info.mid();
    auto it = bundle_groups_by_mid.find(mid);
    const ContentGroup* bundle =
        it != bundle_groups_by_mid.end() ? it->second : nullptr;
    if (bundle && mid != *(bundle->FirstContentName())) {
      // This isn't the first media section in the BUNDLE group, so it's not
      // required to have crypto attributes, since only the crypto attributes
      // from the first section actually get used.
      continue;
    }

    // If the content isn't rejected or bundled into another m= section, crypto
    // must be present.
    const MediaContentDescription* media = content_info.media_description();
    const TransportInfo* tinfo = desc->GetTransportInfoByName(mid);
    if (!media || !tinfo) {
      // Something is not right.
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << kInvalidSdp);
    }
    if (dtls_enabled) {
      if (!tinfo->description.identity_fingerprint) {
        return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                             << kSdpWithoutDtlsFingerprint);
      }
    } else {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << kSdpWithoutCrypto);
    }
  }
  return RTCError::OK();
}

// Checks that each non-rejected content has ice-ufrag and ice-pwd set, unless
// it's in a BUNDLE group, in which case only the BUNDLE-tag section (first
// media section/description in the BUNDLE group) needs a ufrag and pwd.
bool VerifyIceUfragPwdPresent(
    const SessionDescription* desc,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  for (const ContentInfo& content_info : desc->contents()) {
    if (content_info.rejected) {
      continue;
    }
    const auto mid = content_info.mid();
    auto it = bundle_groups_by_mid.find(mid);
    const ContentGroup* bundle =
        it != bundle_groups_by_mid.end() ? it->second : nullptr;
    if (bundle && mid != *(bundle->FirstContentName())) {
      // This isn't the first media section in the BUNDLE group, so it's not
      // required to have ufrag/password, since only the ufrag/password from
      // the first section actually get used.
      continue;
    }

    // If the content isn't rejected or bundled into another m= section,
    // ice-ufrag and ice-pwd must be present.
    const TransportInfo* tinfo = desc->GetTransportInfoByName(mid);
    if (!tinfo) {
      // Something is not right.
      RTC_LOG(LS_ERROR) << kInvalidSdp;
      return false;
    }
    if (tinfo->description.ice_ufrag.empty() ||
        tinfo->description.ice_pwd.empty()) {
      RTC_LOG(LS_ERROR) << "Session description must have ice ufrag and pwd.";
      return false;
    }
  }
  return true;
}

RTCError ValidateMids(const SessionDescription& description) {
  flat_set<absl::string_view> mids;
  for (const ContentInfo& content : description.contents()) {
    if (content.mid().empty()) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "A media section is missing a MID attribute.");
    }
    if (content.mid().size() > kMidMaxSize) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "The MID attribute exceeds the maximum supported "
                              "length of 16 characters.");
    }
    if (!mids.insert(content.mid()).second) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "Duplicate a=mid value '" << content.mid()
                           << "'.");
    }
  }
  return RTCError::OK();
}

RTCError FindDuplicateCodecParameters(
    const RtpCodecParameters codec_parameters,
    std::map<int, RtpCodecParameters>& payload_to_codec_parameters) {
  auto existing_codec_parameters =
      payload_to_codec_parameters.find(codec_parameters.payload_type);
  if (existing_codec_parameters != payload_to_codec_parameters.end() &&
      codec_parameters != existing_codec_parameters->second) {
    StringBuilder sb;
    sb << "A BUNDLE group contains a codec collision between "
       << absl::StrCat(codec_parameters) << " and "
       << absl::StrCat(existing_codec_parameters->second)
       << ". All codecs must share the same type, "
          "encoding name, clock rate and parameters.";

    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << sb.Release());
  }
  payload_to_codec_parameters.insert(
      std::make_pair(codec_parameters.payload_type, codec_parameters));
  return RTCError::OK();
}

RTCError ValidateBundledPayloadTypes(const SessionDescription& description) {
  // https://www.rfc-editor.org/rfc/rfc8843#name-payload-type-pt-value-reuse
  // ... all codecs associated with the payload type number MUST share an
  // identical codec configuration. This means that the codecs MUST share
  // the same media type, encoding name, clock rate, and any parameter
  // that can affect the codec configuration and packetization.
  std::vector<const ContentGroup*> bundle_groups =
      description.GetGroupsByName(GROUP_TYPE_BUNDLE);
  for (const ContentGroup* bundle_group : bundle_groups) {
    std::map<int, RtpCodecParameters> payload_to_codec_parameters;
    for (const std::string& content_name : bundle_group->content_names()) {
      const ContentInfo* content_description =
          description.GetContentByName(content_name);
      if (!content_description) {
        return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                             << "A BUNDLE group contains a MID='"
                             << content_name << "' matching no m= section.");
      }
      const MediaContentDescription* media_description =
          content_description->media_description();
      RTC_DCHECK(media_description);
      if (content_description->rejected || !media_description ||
          !media_description->has_codecs()) {
        continue;
      }
      const auto type = media_description->type();
      if (type == MediaType::AUDIO || type == MediaType::VIDEO) {
        for (const auto& c : media_description->codecs()) {
          auto error = FindDuplicateCodecParameters(
              c.ToCodecParameters(), payload_to_codec_parameters);
          if (!error.ok()) {
            return error;
          }
        }
      }
    }
  }
  return RTCError::OK();
}

RTCError FindDuplicateHeaderExtensionIds(
    const RtpExtension extension,
    std::map<RtpHeaderExtensionId, RtpExtension>& id_to_extension) {
  auto existing_extension = id_to_extension.find(extension.id);
  if (existing_extension != id_to_extension.end() &&
      !(extension.uri == existing_extension->second.uri &&
        extension.encrypt == existing_extension->second.encrypt)) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INVALID_PARAMETER)
        << "A BUNDLE group contains a codec collision for "
           "header extension id="
        << extension.id
        << ". The id must be the same across all bundled media descriptions");
  }
  id_to_extension.insert(std::make_pair(extension.id, extension));
  return RTCError::OK();
}

RTCError ValidateBundledRtpHeaderExtensions(
    const SessionDescription& description) {
  // https://www.rfc-editor.org/rfc/rfc8843#name-rtp-header-extensions-consi
  // ... the identifier used for a given extension MUST identify the same
  // extension across all the bundled media descriptions.
  std::vector<const ContentGroup*> bundle_groups =
      description.GetGroupsByName(GROUP_TYPE_BUNDLE);
  for (const ContentGroup* bundle_group : bundle_groups) {
    std::map<RtpHeaderExtensionId, RtpExtension> id_to_extension;
    for (const std::string& content_name : bundle_group->content_names()) {
      const ContentInfo* content_description =
          description.GetContentByName(content_name);
      if (!content_description) {
        return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                             << "A BUNDLE group contains a MID='"
                             << content_name << "' matching no m= section.");
      }
      const MediaContentDescription* media_description =
          content_description->media_description();
      RTC_DCHECK(media_description);
      if (content_description->rejected || !media_description ||
          !media_description->has_codecs()) {
        continue;
      }

      for (const auto& extension : media_description->rtp_header_extensions()) {
        auto error =
            FindDuplicateHeaderExtensionIds(extension, id_to_extension);
        if (!error.ok()) {
          return error;
        }
      }
    }
  }
  return RTCError::OK();
}

RTCError ValidateRtpHeaderExtensionsForSpecSimulcast(
    const SessionDescription& description) {
  for (const ContentInfo& content : description.contents()) {
    if (content.type != MediaProtocolType::kRtp || content.rejected) {
      continue;
    }
    const auto media_description = content.media_description();
    if (!media_description->HasSimulcast()) {
      continue;
    }
    auto extensions = media_description->rtp_header_extensions();
    auto it = absl::c_find_if(extensions, [](const RtpExtension& ext) {
      return ext.uri == RtpExtension::kRidUri;
    });
    if (it == extensions.end()) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "The media section with MID='" << content.mid()
                           << "' negotiates simulcast but does not negotiate "
                              "the RID RTP header extension.");
    }
  }
  return RTCError::OK();
}

RTCError ValidateSsrcGroups(const SessionDescription& description) {
  for (const ContentInfo& content : description.contents()) {
    if (content.type != MediaProtocolType::kRtp) {
      continue;
    }
    for (const StreamParams& stream : content.media_description()->streams()) {
      for (const SsrcGroup& group : stream.ssrc_groups) {
        // Validate the number of SSRCs for standard SSRC group semantics such
        // as FID and FEC-FR and the non-standard SIM group.
        if ((group.semantics == kFidSsrcGroupSemantics &&
             group.ssrcs.size() != 2) ||
            (group.semantics == kFecFrSsrcGroupSemantics &&
             group.ssrcs.size() != 2) ||
            (group.semantics == kSimSsrcGroupSemantics &&
             group.ssrcs.size() > kMaxSimulcastStreams)) {
          return RTC_LOG_ERROR(
              RTCError(RTCErrorType::INVALID_PARAMETER)
              << "The media section with MID='" << content.mid()
              << "' has a ssrc-group with semantics " << group.semantics
              << " and an unexpected number of SSRCs.");
        }
      }
    }
  }
  return RTCError::OK();
}

// Check that cryptex is supported (if required) and consistent within the
// bundle groups.
RTCError ValidateCryptex(
    const SessionDescription* description,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid,
    bool required_to_negotiate) {
  RTC_DCHECK(description);

  bool session_level_support = description->cryptex();
  for (const ContentInfo& content_info : description->contents()) {
    if (content_info.rejected ||
        !IsRtpProtocol(content_info.media_description()->protocol())) {
      continue;
    }
    const MediaContentDescription* media = content_info.media_description();
    if (required_to_negotiate && !session_level_support && !media->cryptex()) {
      return RTC_LOG_ERROR(RTCError::InvalidParameter()
                           << "cryptex required but not negotiated.");
    }

    if (media->cryptex_level() ==
        MediaContentDescription::AttributeLevel::kSession) {
      // Session level attributes are implicitly consistent.
      continue;
    }

    const auto mid = content_info.mid();
    auto it = bundle_groups_by_mid.find(mid);
    if (it == bundle_groups_by_mid.end() || it->second == nullptr) {
      continue;
    }
    const ContentGroup* bundle = it->second;

    // Find and compare cryptex at media level of the current content to
    // cryptex of the first non-datachannel media content.
    const ContentInfo* first_media_content = nullptr;
    for (const std::string& name : bundle->content_names()) {
      const ContentInfo* content = description->GetContentByName(name);
      if (content && IsRtpProtocol(content->media_description()->protocol())) {
        first_media_content = content;
        break;
      }
    }
    if (!first_media_content) {
      return RTC_LOG_ERROR(RTCError::InvalidParameter()
                           << "Inconsistent bundle");
    }
    // If this content is the first non-datachannel media content in the
    // bundle, this is implicitly consistent.
    if (first_media_content->mid() == mid) {
      continue;
    }
    if (first_media_content->media_description()->cryptex_level() !=
        media->cryptex_level()) {
      return RTC_LOG_ERROR(
          RTCError::InvalidParameter()
          << "The media section with MID='" << content_info.mid()
          << "' is not consistent with the cryptex level of its bundle");
    }
  }
  return RTCError::OK();
}

RTCError ValidatePayloadTypes(const SessionDescription& description) {
  for (const ContentInfo& content : description.contents()) {
    if (content.type != MediaProtocolType::kRtp) {
      continue;
    }
    const auto media_description = content.media_description();
    RTC_DCHECK(media_description);
    if (content.rejected || !media_description ||
        !media_description->has_codecs()) {
      continue;
    }
    const auto type = media_description->type();
    if (type == MediaType::AUDIO || type == MediaType::VIDEO) {
      for (const auto& codec : media_description->codecs()) {
        if (!PayloadType::IsValid(codec.id, media_description->rtcp_mux())) {
          return RTC_LOG_ERROR(
              RTCError(RTCErrorType::INVALID_PARAMETER)
              << "The media section with MID='" << content.mid()
              << "' used an invalid payload type " << codec.id << " for codec '"
              << codec.name << ", rtcp-mux:"
              << (media_description->rtcp_mux() ? "enabled" : "disabled"));
        }
      }
    }
  }
  return RTCError::OK();
}

bool IsValidOfferToReceiveMedia(int value) {
  typedef PeerConnectionInterface::RTCOfferAnswerOptions Options;
  return (value >= Options::kUndefined) &&
         (value <= Options::kMaxOfferToReceiveMedia);
}

bool ValidateOfferAnswerOptions(
    const PeerConnectionInterface::RTCOfferAnswerOptions& rtc_options) {
  return IsValidOfferToReceiveMedia(rtc_options.offer_to_receive_audio) &&
         IsValidOfferToReceiveMedia(rtc_options.offer_to_receive_video);
}

// This method will extract any send encodings that were sent by the remote
// connection. This is currently only relevant for Simulcast scenario (where
// the number of layers may be communicated by the server).
std::vector<RtpEncodingParameters> GetSendEncodingsFromRemoteDescription(
    const MediaContentDescription& desc) {
  if (!desc.HasSimulcast()) {
    return {};
  }
  std::vector<RtpEncodingParameters> result;
  const SimulcastDescription& simulcast = desc.simulcast_description();

  // This is a remote description, the parameters we are after should appear
  // as receive streams.
  for (const auto& alternatives : simulcast.receive_layers()) {
    if (result.size() >= kMaxSimulcastStreams) {
      RTC_LOG(LS_WARNING)
          << "Excessive simulcast layers in remote description. Clamping to "
          << kMaxSimulcastStreams;
      break;
    }
    RTC_DCHECK(!alternatives.empty());
    // There is currently no way to specify or choose from alternatives.
    // We will always use the first alternative, which is the most preferred.
    const SimulcastLayer& layer = alternatives[0];
    RtpEncodingParameters parameters;
    parameters.rid = layer.rid;
    parameters.active = !layer.is_paused;
    // If a payload type has been specified for this rid, set the codec
    // corresponding to that payload type.
    auto rid_desc = std::find_if(
        desc.receive_rids().begin(), desc.receive_rids().end(),
        [&layer](const RidDescription& rid) { return rid.rid == layer.rid; });
    if (rid_desc != desc.receive_rids().end() && !rid_desc->codecs.empty()) {
      parameters.codec = rid_desc->codecs[0].ToCodecParameters();
    }
    result.push_back(parameters);
  }

  return result;
}

RTCError UpdateSimulcastLayerStatusInSender(
    const std::vector<SimulcastLayer>& layers,
    scoped_refptr<RtpSenderInternal> sender) {
  RTC_DCHECK(sender);
  RTC_LOG_THREAD_BLOCK_COUNT();
  // In practice simulcast is supported only for video. However, this can
  // currently get called for non-video senders in tests.
  // See the `external/wpt/webrtc/simulcast/negotiation-encodings.https.html`
  // test in chromium.
  RtpParameters parameters = sender->GetParametersInternalWithAllLayers();
  std::vector<std::string> disabled_layers;
  bool changed = false;

  // The simulcast envelope cannot be changed, only the status of the streams.
  // So we will iterate over the send encodings rather than the layers.
  for (RtpEncodingParameters& encoding : parameters.encodings) {
    auto iter = std::find_if(layers.begin(), layers.end(),
                             [&encoding](const SimulcastLayer& layer) {
                               return layer.rid == encoding.rid;
                             });
    // A layer that cannot be found may have been removed by the remote party.
    if (iter == layers.end()) {
      disabled_layers.push_back(encoding.rid);
      continue;
    }

    if (encoding.active != !iter->is_paused) {
      encoding.active = !iter->is_paused;
      changed = true;
    }
  }

  RTCError result = RTCError::OK();
  if (changed) {
    result = sender->SetParametersInternalWithAllLayers(parameters);
    if (!result.ok()) {
      return result;
    }
  }

  if (!disabled_layers.empty()) {
    bool need_disable = false;
    for (const auto& rid : disabled_layers) {
      auto iter = std::find_if(
          parameters.encodings.begin(), parameters.encodings.end(),
          [&rid](const RtpEncodingParameters& enc) { return enc.rid == rid; });
      if (iter != parameters.encodings.end() && iter->active) {
        need_disable = true;
        break;
      }
    }

    if (need_disable) {
      result = sender->DisableEncodingLayers(disabled_layers);
    }
  }

  return result;
}

bool SimulcastIsRejected(const ContentInfo* local_content,
                         const MediaContentDescription& answer_media_desc,
                         bool enable_encrypted_rtp_header_extensions) {
  bool simulcast_offered = local_content &&
                           local_content->media_description() &&
                           local_content->media_description()->HasSimulcast();
  bool simulcast_answered = answer_media_desc.HasSimulcast();
  bool rids_supported = RtpExtension::FindHeaderExtensionByUri(
      answer_media_desc.rtp_header_extensions(), RtpExtension::kRidUri,
      enable_encrypted_rtp_header_extensions
          ? RtpExtension::Filter::kPreferEncryptedExtension
          : RtpExtension::Filter::kDiscardEncryptedExtension);
  return simulcast_offered && (!simulcast_answered || !rids_supported);
}

bool GetInitialSimulcastLayersAndRejectionStatus(
    ContentSource source,
    const MediaContentDescription& media_desc,
    const ContentInfo* old_local_content,
    bool enable_encrypted_rtp_header_extensions,
    std::vector<SimulcastLayer>& initial_simulcast_layers) {
  if (media_desc.HasSimulcast()) {
    initial_simulcast_layers =
        source == CS_LOCAL
            ? media_desc.simulcast_description().send_layers().GetAllLayers()
            : media_desc.simulcast_description()
                  .receive_layers()
                  .GetAllLayers();
  }

  return SimulcastIsRejected(old_local_content, media_desc,
                             enable_encrypted_rtp_header_extensions);
}

RTCError DisableSimulcastInSender(scoped_refptr<RtpSenderInternal> sender) {
  RTC_DCHECK(sender);
  RTC_LOG_THREAD_BLOCK_COUNT();
  RtpParameters parameters = sender->GetParametersInternalWithAllLayers();
  if (parameters.encodings.size() <= 1) {
    return RTCError::OK();
  }

  std::vector<std::string> disabled_layers;
  for (size_t i = 1; i < parameters.encodings.size(); ++i) {
    if (parameters.encodings[i].active) {
      disabled_layers.push_back(parameters.encodings[i].rid);
    }
  }

  if (!disabled_layers.empty()) {
    return sender->DisableEncodingLayers(disabled_layers);
  }
  return RTCError::OK();
}

// The SDP parser used to populate these values by default for the 'content
// name' if an a=mid line was absent.
PLAN_B_ONLY std::string GetDefaultMidForPlanB(MediaType media_type) {
  switch (media_type) {
    case MediaType::AUDIO:
      return CN_AUDIO;
    case MediaType::VIDEO:
      return CN_VIDEO;
    case MediaType::DATA:
      return CN_DATA;
    case MediaType::UNSUPPORTED:
      return "not supported";
    default:
      // Fall through to RTC_CHECK_NOTREACHED
      break;
  }
  RTC_DCHECK_NOTREACHED();
  return "";
}

// Add options to |[audio/video]_media_description_options| from `senders`.
PLAN_B_ONLY void AddPlanBRtpSenderOptions(
    const std::vector<
        scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>>& senders,
    MediaDescriptionOptions* audio_media_description_options,
    MediaDescriptionOptions* video_media_description_options,
    int num_sim_layers) {
  for (const auto& sender : senders) {
    if (sender->media_type() == MediaType::AUDIO) {
      if (audio_media_description_options) {
        audio_media_description_options->AddAudioSender(
            sender->id(), sender->internal()->stream_ids());
      }
    } else {
      RTC_DCHECK(sender->media_type() == MediaType::VIDEO);
      if (video_media_description_options) {
        video_media_description_options->AddVideoSender(
            sender->id(), sender->internal()->stream_ids(), {},
            SimulcastLayerList(), num_sim_layers);
      }
    }
  }
}

MediaDescriptionOptions GetMediaDescriptionOptionsForTransceiver(
    RtpTransceiver* transceiver,
    absl::string_view mid,
    bool is_create_offer) {
  // NOTE: a stopping transceiver should be treated as a stopped one in
  // createOffer as specified in
  // https://w3c.github.io/webrtc-pc/#dom-rtcpeerconnection-createoffer.
  bool stopped =
      is_create_offer ? transceiver->stopping() : transceiver->stopped();
  MediaDescriptionOptions media_description_options(
      transceiver->media_type(), mid, transceiver->direction(), stopped);
  media_description_options.codec_preferences =
      transceiver->filtered_codec_preferences();
  media_description_options.header_extensions =
      transceiver->GetHeaderExtensionsToNegotiate();
  media_description_options.sframe_enabled =
      transceiver->SframeEnabled().value_or(false);
  // This behavior is specified in JSEP. The gist is that:
  // 1. The MSID is included if the RtpTransceiver's direction is sendonly or
  //    sendrecv.
  // 2. If the MSID is included, then it must be included in any subsequent
  //    offer/answer exactly the same until the RtpTransceiver is stopped.
  if (stopped || (!RtpTransceiverDirectionHasSend(transceiver->direction()) &&
                  !transceiver->has_ever_been_used_to_send())) {
    return media_description_options;
  }

  SenderOptions sender_options;
  sender_options.track_id = transceiver->sender()->id();
  sender_options.stream_ids = transceiver->sender()->stream_ids();

  // The following sets up RIDs and Simulcast.
  // RIDs are included if Simulcast is requested or if any RID was specified.
  RtpParameters send_parameters =
      transceiver->sender_internal()->GetParametersInternalWithAllLayers();
  bool has_rids = std::any_of(send_parameters.encodings.begin(),
                              send_parameters.encodings.end(),
                              [](const RtpEncodingParameters& encoding) {
                                return !encoding.rid.empty();
                              });

  std::vector<RidDescription> send_rids;
  SimulcastLayerList send_layers;
  for (const RtpEncodingParameters& encoding : send_parameters.encodings) {
    if (encoding.rid.empty()) {
      continue;
    }
    auto send_rid = RidDescription(encoding.rid, RidDirection::kSend);
    if (encoding.codec) {
      auto send_codecs = transceiver->sender_internal()->GetSendCodecs();
      for (const Codec& codec : send_codecs) {
        if (IsSameRtpCodecIgnoringLevel(codec, *encoding.codec)) {
          send_rid.codecs.push_back(codec);
          break;
        }
      }
    }
    send_rids.push_back(send_rid);
    send_layers.AddLayer(SimulcastLayer(encoding.rid, !encoding.active));
  }

  if (has_rids) {
    sender_options.rids = send_rids;
  }

  sender_options.simulcast_layers = send_layers;
  // When RIDs are configured, we must set num_sim_layers to 0 to.
  // Otherwise, num_sim_layers must be 1 because either there is no
  // simulcast, or simulcast is acheived by munging the SDP.
  sender_options.num_sim_layers = has_rids ? 0 : 1;
  media_description_options.sender_options.push_back(sender_options);

  return media_description_options;
}

// Returns the ContentInfo at mline index `i`, or null if none exists.
const ContentInfo* GetContentByIndex(const SessionDescriptionInterface* sdesc,
                                     size_t i) {
  if (!sdesc) {
    return nullptr;
  }
  const ContentInfos& contents = sdesc->description()->contents();
  return (i < contents.size() ? &contents[i] : nullptr);
}

// From `rtc_options`, fill parts of `session_options` shared by all generated
// m= sections (in other words, nothing that involves a map/array).
void ExtractSharedMediaSessionOptions(
    const PeerConnectionInterface::RTCOfferAnswerOptions& rtc_options,
    MediaSessionOptions* session_options) {
  session_options->vad_enabled = rtc_options.voice_activity_detection;
  session_options->bundle_enabled = rtc_options.use_rtp_mux;
  session_options->raw_packetization_for_video =
      rtc_options.raw_packetization_for_video;
}

// Generate a RTCP CNAME when a PeerConnection is created.
std::string GenerateRtcpCname() {
  std::string cname;
  if (!CreateRandomString(kRtcpCnameLength, &cname)) {
    RTC_LOG(LS_ERROR) << "Failed to generate CNAME.";
    RTC_DCHECK_NOTREACHED();
  }
  return cname;
}

// Check if we can send `new_stream` on a PeerConnection.
bool CanAddLocalMediaStream(StreamCollectionInterface* current_streams,
                            MediaStreamInterface* new_stream) {
  if (!new_stream || !current_streams) {
    return false;
  }
  if (current_streams->find(new_stream->id()) != nullptr) {
    RTC_LOG(LS_ERROR) << "MediaStream with ID " << new_stream->id()
                      << " is already added.";
    return false;
  }
  return true;
}

bool ContentHasHeaderExtension(const ContentInfo& content_info,
                               absl::string_view header_extension_uri) {
  for (const RtpExtension& rtp_header_extension :
       content_info.media_description()->rtp_header_extensions()) {
    if (rtp_header_extension.uri == header_extension_uri) {
      return true;
    }
  }
  return false;
}

// Matches the given host with the wildcard host. A wildcard `*` matches the
// current character and anything that comes after it. For instance, 127.0.*
// matches any string that starts with `127.0.`. For IPv6 addresses, the
// wildcard host should also start with `[` and end with `]` or a wildcard.
bool WildcardHostPrefixMatch(absl::string_view host,
                             absl::string_view wildcard_host) {
  for (size_t i = 0; i < std::min(host.size(), wildcard_host.size()); ++i) {
    if (host[i] == wildcard_host[i]) {
      continue;
    }
    return wildcard_host[i] == '*';
  }
  return host.size() == wildcard_host.size();
}

#if RTC_DCHECK_IS_ON
bool SessionDescriptionStructureEquals(
    const SessionDescriptionInterface& desc_a,
    const SessionDescriptionInterface& desc_b) {
  const SessionDescription* a = desc_a.description();
  const SessionDescription* b = desc_b.description();
  if (a == nullptr || b == nullptr) {
    return a == b;
  }
  if (a->contents().size() != b->contents().size()) {
    return false;
  }
  for (size_t i = 0; i < a->contents().size(); ++i) {
    const auto& content_a = a->contents()[i];
    const auto& content_b = b->contents()[i];
    if (content_a.media_description()->type() !=
        content_b.media_description()->type()) {
      return false;
    }
    if (content_a.media_description()->protocol() !=
        content_b.media_description()->protocol()) {
      return false;
    }
    if (content_a.media_description()->direction() !=
        content_b.media_description()->direction()) {
      return false;
    }
    if (content_a.media_description()->has_codecs() !=
        content_b.media_description()->has_codecs()) {
      return false;
    }
    if (content_a.media_description()->has_codecs() &&
        content_a.media_description()->codecs() !=
            content_b.media_description()->codecs()) {
      return false;
    }
    if (content_a.media_description()->rtp_header_extensions() !=
        content_b.media_description()->rtp_header_extensions()) {
      return false;
    }
  }
  if (a->groups().size() != b->groups().size()) {
    return false;
  }
  for (size_t i = 0; i < a->groups().size(); ++i) {
    if (a->groups()[i].semantics() != b->groups()[i].semantics()) {
      return false;
    }
    if (a->groups()[i].content_names().size() !=
        b->groups()[i].content_names().size()) {
      return false;
    }
  }
  if (a->transport_infos().size() != b->transport_infos().size()) {
    return false;
  }
  for (size_t i = 0; i < a->transport_infos().size(); ++i) {
    const auto& transport_a = a->transport_infos()[i];
    const auto& transport_b = b->transport_infos()[i];
    if (transport_a.description.connection_role !=
        transport_b.description.connection_role) {
      return false;
    }
    const auto& fp_a = transport_a.description.identity_fingerprint;
    const auto& fp_b = transport_b.description.identity_fingerprint;
    if ((fp_a == nullptr) != (fp_b == nullptr)) {
      return false;
    }
    if (fp_a != nullptr && *fp_a != *fp_b) {
      return false;
    }
  }
  return true;
}

class CreateSessionDescriptionObserverVerify
    : public CreateSessionDescriptionObserver {
 public:
  CreateSessionDescriptionObserverVerify(
      std::unique_ptr<SessionDescriptionInterface> expected,
      absl::AnyInvocable<void() &&> on_verified)
      : expected_(std::move(expected)), on_verified_(std::move(on_verified)) {}
  void OnSuccess(SessionDescriptionInterface* desc) override {
    std::unique_ptr<SessionDescriptionInterface> generated(desc);
    RTC_DCHECK(SessionDescriptionStructureEquals(*expected_, *generated));
    std::move(on_verified_)();
  }
  void OnFailure(RTCError error) override {
    RTC_DCHECK_NOTREACHED() << "Cache verification failed: " << error.message();
  }

 private:
  std::unique_ptr<SessionDescriptionInterface> expected_;
  absl::AnyInvocable<void() &&> on_verified_;
};
#endif
}  // namespace

void UpdateRtpHeaderExtensionPreferencesFromSdpMunging(
    const SessionDescription* description,
    TransceiverList* transceivers) {
  // This integrates the RTP Header Extension Control API and local SDP munging
  // for backward compability reasons. If something was enabled in the local
  // description via SDP munging, consider it non-stopped in the API as well
  // so that is shows up in subsequent offers/answers.
  RTC_DCHECK(description);
  RTC_DCHECK(transceivers);
  for (const auto& content : description->contents()) {
    auto transceiver = transceivers->FindByMid(content.mid());
    if (!transceiver) {
      continue;
    }
    auto extension_capabilities = transceiver->GetHeaderExtensionsToNegotiate();
    // Set the capability of every extension we see here to "sendrecv".
    for (auto& ext : content.media_description()->rtp_header_extensions()) {
      auto it = absl::c_find_if(extension_capabilities,
                                [&ext](const RtpHeaderExtensionCapability c) {
                                  return ext.uri == c.uri;
                                });
      if (it != extension_capabilities.end()) {
        it->direction = RtpTransceiverDirection::kSendRecv;
      }
    }
    transceiver->SetHeaderExtensionsToNegotiate(extension_capabilities);
  }
}


// This class stores state related to a SetRemoteDescription operation, captures
// and reports potential errors that might occur and makes sure to notify the
// observer of the operation and the operations chain of completion.
class SdpOfferAnswerHandler::RemoteDescriptionOperation {
 public:
  RemoteDescriptionOperation(
      SdpOfferAnswerHandler* handler,
      std::unique_ptr<SessionDescriptionInterface> desc,
      scoped_refptr<SetRemoteDescriptionObserverInterface> observer,
      std::function<void()> operations_chain_callback)
      : handler_(handler),
        desc_(std::move(desc)),
        observer_(std::move(observer)),
        operations_chain_callback_(std::move(operations_chain_callback)),
        type_(desc_->GetType()),
        unified_plan_(handler_->IsUnifiedPlan()) {}

  ~RemoteDescriptionOperation() {
    RTC_DCHECK_RUN_ON(handler_->signaling_thread());
    SignalCompletion();
    operations_chain_callback_();
  }

  bool ok() const { return error_.ok(); }

  // Notifies the observer that the operation is complete and releases the
  // reference to the observer.
  void SignalCompletion() {
    if (!observer_)
      return;

    if (!error_.ok() && type_ != static_cast<SdpType>(-1)) {
      std::string error_message =
          GetSetDescriptionErrorMessage(CS_REMOTE, type_, error_);
      RTC_LOG(LS_ERROR) << error_message;
      error_.set_message(error_message);
    }

    if (handler_) {
      handler_->TraceSetRemoteDescriptionComplete(error_);
    }
    observer_->OnSetRemoteDescriptionComplete(error_);
    observer_ = nullptr;  // Only fire the notification once.
  }

  // If a session error has occurred the PeerConnection is in a possibly
  // inconsistent state so fail right away.
  bool HaveSessionError() {
    RTC_DCHECK(ok());
    if (handler_->session_error() != SessionError::kNone)
      InternalError(handler_->GetSessionErrorMsg());
    return !ok();
  }

  // Returns true if the operation was a rollback operation. If this function
  // returns true, the caller should consider the operation complete. Otherwise
  // proceed to the next step.
  bool MaybeRollback() {
    RTC_DCHECK_RUN_ON(handler_->signaling_thread());
    RTC_DCHECK(ok());
    if (type_ != SdpType::kRollback) {
      // Check if we can do an implicit rollback.
      if (type_ == SdpType::kOffer && unified_plan_ &&
          handler_->pc_->configuration()->enable_implicit_rollback &&
          handler_->signaling_state() ==
              PeerConnectionInterface::kHaveLocalOffer) {
        handler_->Rollback(type_);
      }
      return false;
    }

    if (unified_plan_) {
      error_ = handler_->Rollback(type_);
    } else if (type_ == SdpType::kRollback) {
      Unsupported("Rollback not supported in Plan B");
    }

    return true;
  }

  // Report to UMA the format of the received offer or answer.
  void ReportOfferAnswerUma() {
    RTC_DCHECK(ok());
    if (type_ == SdpType::kOffer || type_ == SdpType::kAnswer) {
      handler_->pc_->ReportSdpBundleUsage(*desc_);
    }
  }

  // Checks if the session description for the operation is valid. If not, the
  // function captures error information and returns false. Note that if the
  // return value is false, the operation should be considered done.
  bool IsDescriptionValid() {
    RTC_DCHECK_RUN_ON(handler_->signaling_thread());
    RTC_DCHECK(ok());
    RTC_DCHECK(bundle_groups_by_mid_.empty()) << "Already called?";
    bundle_groups_by_mid_ = GetBundleGroupsByMid(description());
    error_ = handler_->ValidateSessionDescription(desc_.get(), CS_REMOTE,
                                                  bundle_groups_by_mid_);
    return ok();
  }

  // Transfers ownership of the session description object over to `handler_`.
  bool ReplaceRemoteDescriptionAndCheckError() {
    RTC_DCHECK_RUN_ON(handler_->signaling_thread());
    RTC_DCHECK(ok());
    RTC_DCHECK(desc_);
    RTC_DCHECK(!replaced_remote_description_);
#if RTC_DCHECK_IS_ON
    const auto* existing_remote_description = handler_->remote_description();
#endif

    error_ = handler_->ReplaceRemoteDescription(std::move(desc_), type_,
                                                &replaced_remote_description_);

    if (ok()) {
#if RTC_DCHECK_IS_ON
      // Sanity check that our `old_remote_description()` method always returns
      // the same value as `remote_description()` did before the call to
      // ReplaceRemoteDescription.
      RTC_DCHECK_EQ(existing_remote_description, old_remote_description());
#endif
    } else {
      SetAsSessionError();
    }

    return ok();
  }

  bool UpdateChannels() {
    RTC_DCHECK(ok());
    RTC_DCHECK(!desc_) << "ReplaceRemoteDescription hasn't been called";

    const auto* remote_description = handler_->remote_description();

    const SessionDescription* session_desc = remote_description->description();

    // Transport and Media channels will be created only when offer is set.
    if (unified_plan_) {
      error_ = handler_->UpdateTransceiversAndDataChannels(
          CS_REMOTE, *remote_description, handler_->local_description(),
          old_remote_description(), bundle_groups_by_mid_);
    } else {
      // Media channels will be created only when offer is set. These may use
      // new transports just created by PushdownTransportDescription.
      if (type_ == SdpType::kOffer) {
        // TODO(mallinath) - Handle CreateChannel failure, as new local
        // description is applied. Restore back to old description.
        RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
        error_ = handler_->CreateChannels(*session_desc);
        RTC_ALLOW_PLAN_B_DEPRECATION_END();
      }
      // Remove unused channels if MediaContentDescription is rejected.
      handler_->RemoveUnusedChannels(session_desc);
    }

    return ok();
  }

  bool UpdateSessionState() {
    RTC_DCHECK(ok());
    error_ = handler_->UpdateSessionState(
        type_, CS_REMOTE, handler_->remote_description()->description(),
        bundle_groups_by_mid_);
    if (!ok())
      SetAsSessionError();
    return ok();
  }

  bool UseCandidatesInRemoteDescription() {
    RTC_DCHECK(ok());
    if (handler_->local_description() &&
        !handler_->UseCandidatesInRemoteDescription()) {
      InvalidParam(kInvalidCandidates);
    }
    return ok();
  }

  // Convenience getter for desc_->GetType().
  SdpType type() const { return type_; }
  bool unified_plan() const { return unified_plan_; }
  SessionDescription* description() { return desc_->description(); }

  const SessionDescriptionInterface* old_remote_description() const {
    RTC_DCHECK(!desc_) << "Called before replacing the remote description";
    if (type_ == SdpType::kAnswer)
      return replaced_remote_description_.get();
    return replaced_remote_description_
               ? replaced_remote_description_.get()
               : handler_->current_remote_description();
  }

  // Returns a reference to a cached map of bundle groups ordered by mid.
  // Note that this will only be valid after a successful call to
  // `IsDescriptionValid`.
  const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid()
      const {
    RTC_DCHECK(ok());
    return bundle_groups_by_mid_;
  }

 private:
  // Convenience methods for populating the embedded `error_` object.
  void Unsupported(absl::string_view message) {
    SetError(RTCErrorType::UNSUPPORTED_OPERATION, message);
  }

  void InvalidParam(absl::string_view message) {
    SetError(RTCErrorType::INVALID_PARAMETER, message);
  }

  void InternalError(absl::string_view message) {
    SetError(RTCErrorType::INTERNAL_ERROR, message);
  }

  void SetError(RTCErrorType type, absl::string_view message) {
    RTC_DCHECK(ok()) << "Overwriting an existing error?";
    error_ = RTCError(type, message);
  }

  // Called when the PeerConnection could be in an inconsistent state and we set
  // the session error so that future calls to
  // SetLocalDescription/SetRemoteDescription fail.
  void SetAsSessionError() {
    RTC_DCHECK(!ok());
    handler_->SetSessionError(SessionError::kContent, error_.message());
  }

  SdpOfferAnswerHandler* const handler_;
  std::unique_ptr<SessionDescriptionInterface> desc_;
  // Keeps the replaced session description object alive while the operation
  // is taking place since methods that depend on `old_remote_description()`
  // for updating the state, need it.
  std::unique_ptr<SessionDescriptionInterface> replaced_remote_description_;
  scoped_refptr<SetRemoteDescriptionObserverInterface> observer_;
  std::function<void()> operations_chain_callback_;
  RTCError error_ = RTCError::OK();
  flat_map<std::string, const ContentGroup*> bundle_groups_by_mid_;
  const SdpType type_;
  const bool unified_plan_;
};
// Used by parameterless SetLocalDescription() to create an offer or answer.
// Upon completion of creating the session description, SetLocalDescription() is
// invoked with the result.
class SdpOfferAnswerHandler::ImplicitCreateSessionDescriptionObserver
    : public CreateSessionDescriptionObserver {
 public:
  ImplicitCreateSessionDescriptionObserver(
      WeakPtr<SdpOfferAnswerHandler> sdp_handler,
      scoped_refptr<SetLocalDescriptionObserverInterface>
          set_local_description_observer)
      : sdp_handler_(std::move(sdp_handler)),
        set_local_description_observer_(
            std::move(set_local_description_observer)) {}
  ~ImplicitCreateSessionDescriptionObserver() override {
    RTC_DCHECK(was_called_);
  }

  void SetOperationCompleteCallback(
      std::function<void()> operation_complete_callback) {
    operation_complete_callback_ = std::move(operation_complete_callback);
  }

  bool was_called() const { return was_called_; }

  void OnSuccess(SessionDescriptionInterface* desc_ptr) override {
    RTC_DCHECK(!was_called_);
    std::unique_ptr<SessionDescriptionInterface> desc(desc_ptr);
    was_called_ = true;

    // Abort early if `pc_` is no longer valid.
    if (!sdp_handler_) {
      operation_complete_callback_();
      return;
    }
    // DoSetLocalDescription() is a synchronous operation that invokes
    // `set_local_description_observer_` with the result.
    sdp_handler_->DoSetLocalDescription(
        std::move(desc), std::move(set_local_description_observer_));
    operation_complete_callback_();
  }

  void OnFailure(RTCError error) override {
    RTC_DCHECK(!was_called_);
    was_called_ = true;
    error = RTCError(error.type(),
                     "SetLocalDescription failed to create "
                     "session description - ")
            << error.message();
    if (sdp_handler_) {
      sdp_handler_->TraceSetLocalDescriptionComplete(error);
    }
    set_local_description_observer_->OnSetLocalDescriptionComplete(
        std::move(error));
    operation_complete_callback_();
  }

 private:
  bool was_called_ = false;
  WeakPtr<SdpOfferAnswerHandler> sdp_handler_;
  scoped_refptr<SetLocalDescriptionObserverInterface>
      set_local_description_observer_;
  std::function<void()> operation_complete_callback_;
};

// Wraps a CreateSessionDescriptionObserver and an OperationsChain operation
// complete callback. When the observer is invoked, the wrapped observer is
// invoked followed by invoking the completion callback.
class SdpOfferAnswerHandler::CreateSessionDescriptionObserverOperationWrapper
    : public CreateSessionDescriptionObserver {
 public:
  CreateSessionDescriptionObserverOperationWrapper(
      scoped_refptr<CreateSessionDescriptionObserver> observer,
      absl::AnyInvocable<void() &&> operation_complete_callback,
      WeakPtr<SdpOfferAnswerHandler> sdp_handler,
      SdpType type)
      : observer_(std::move(observer)),
        operation_complete_callback_(std::move(operation_complete_callback)),
        sdp_handler_(std::move(sdp_handler)),
        type_(type) {
    RTC_DCHECK(observer_);
  }
  ~CreateSessionDescriptionObserverOperationWrapper() override {
#if RTC_DCHECK_IS_ON
    RTC_DCHECK(was_called_);
#endif
  }

  void OnSuccess(SessionDescriptionInterface* desc) override {
#if RTC_DCHECK_IS_ON
    RTC_DCHECK(!was_called_);
    was_called_ = true;
#endif  // RTC_DCHECK_IS_ON
    // Trace before completing the operation, which may destroy the
    // PeerConnection that owns the tracer.
    if (sdp_handler_) {
      sdp_handler_->TraceCreateSessionDescriptionComplete(type_, desc,
                                                          RTCError::OK());
    }
    scoped_refptr<CreateSessionDescriptionObserver> observer = observer_;
    auto operation_complete_callback = std::move(operation_complete_callback_);
    observer->OnOperationComplete(std::move(operation_complete_callback));
    desc->RelinquishThreadOwnership();
    observer->OnSuccess(desc);
  }

  void OnFailure(RTCError error) override {
#if RTC_DCHECK_IS_ON
    RTC_DCHECK(!was_called_);
    was_called_ = true;
#endif  // RTC_DCHECK_IS_ON
    if (sdp_handler_) {
      sdp_handler_->TraceCreateSessionDescriptionComplete(type_, nullptr,
                                                          error);
    }
    scoped_refptr<CreateSessionDescriptionObserver> observer = observer_;
    auto operation_complete_callback = std::move(operation_complete_callback_);
    observer->OnOperationComplete(std::move(operation_complete_callback));
    observer->OnFailure(std::move(error));
  }

 private:
#if RTC_DCHECK_IS_ON
  bool was_called_ = false;
#endif  // RTC_DCHECK_IS_ON
  scoped_refptr<CreateSessionDescriptionObserver> observer_;
  absl::AnyInvocable<void() &&> operation_complete_callback_;
  const WeakPtr<SdpOfferAnswerHandler> sdp_handler_;
  const SdpType type_;
};

// Wraps a session description observer so a Clone of the last created
// offer/answer can be stored.
// This object can be used only once.
class CreateDescriptionObserverWrapperWithCreationCallback
    : public CreateSessionDescriptionObserver {
 public:
  CreateDescriptionObserverWrapperWithCreationCallback(
      absl::AnyInvocable<void(std::unique_ptr<SessionDescriptionInterface>) &&>
          callback,
      scoped_refptr<CreateSessionDescriptionObserver> observer)
      : callback_(std::move(callback)), observer_(observer) {
    RTC_DCHECK(observer_);
  }
  void OnSuccess(SessionDescriptionInterface* desc) override {
    // `OnSuccess` will be called on the signaling thread. That's what we want
    // for SdpOfferAnswerHandler since that's where public methods are called.
    // The wrapped observer might have different threading needs. Calling the
    // `Clone()` method will require accessing internal state of `desc` and
    // therefore will (at least semantically) attach its state to the current
    // thread. So, since we need to call Clone(), keep `desc` as the copy that
    // SdpOfferAnswerHandler will own, and give a pristine clone to the
    // observer.
    auto clone = desc->Clone();
    std::move(callback_)(absl::WrapUnique(desc));
    observer_->OnSuccess(clone.release());
  }
  void OnFailure(RTCError error) override {
    std::move(callback_)(nullptr);
    observer_->OnFailure(std::move(error));
  }

 private:
  absl::AnyInvocable<void(std::unique_ptr<SessionDescriptionInterface>) &&>
      callback_;
  scoped_refptr<CreateSessionDescriptionObserver> observer_;
};

// Wrapper for SetSessionDescriptionObserver that invokes the success or failure
// callback in a posted message handled by the peer connection. This introduces
// a delay that prevents recursive API calls by the observer, but this also
// means that the PeerConnection can be modified before the observer sees the
// result of the operation. This is ill-advised for synchronizing states.
//
// Implements both the SetLocalDescriptionObserverInterface and the
// SetRemoteDescriptionObserverInterface.
class SdpOfferAnswerHandler::SetSessionDescriptionObserverAdapter
    : public SetLocalDescriptionObserverInterface,
      public SetRemoteDescriptionObserverInterface {
 public:
  SetSessionDescriptionObserverAdapter(
      WeakPtr<SdpOfferAnswerHandler> handler,
      scoped_refptr<SetSessionDescriptionObserver> inner_observer)
      : handler_(std::move(handler)),
        inner_observer_(std::move(inner_observer)) {}

  // SetLocalDescriptionObserverInterface implementation.
  void OnSetLocalDescriptionComplete(RTCError error) override {
    OnSetDescriptionComplete(std::move(error));
  }
  // SetRemoteDescriptionObserverInterface implementation.
  void OnSetRemoteDescriptionComplete(RTCError error) override {
    OnSetDescriptionComplete(std::move(error));
  }

 private:
  void OnSetDescriptionComplete(RTCError error) {
    if (!handler_)
      return;
    if (error.ok()) {
      handler_->pc_->message_handler()->PostSetSessionDescriptionSuccess(
          inner_observer_.get());
    } else {
      handler_->pc_->message_handler()->PostSetSessionDescriptionFailure(
          inner_observer_.get(), std::move(error));
    }
  }

  WeakPtr<SdpOfferAnswerHandler> handler_;
  scoped_refptr<SetSessionDescriptionObserver> inner_observer_;
};

class SdpOfferAnswerHandler::LocalIceCredentialsToReplace {
 public:
  // Sets the ICE credentials that need restarting to the ICE credentials of
  // the current and pending descriptions.
  void SetIceCredentialsFromLocalDescriptions(
      const SessionDescriptionInterface* current_local_description,
      const SessionDescriptionInterface* pending_local_description) {
    ice_credentials_.clear();
    if (current_local_description) {
      AppendIceCredentialsFromSessionDescription(*current_local_description);
    }
    if (pending_local_description) {
      AppendIceCredentialsFromSessionDescription(*pending_local_description);
    }
  }

  void ClearIceCredentials() { ice_credentials_.clear(); }

  // Returns true if we have ICE credentials that need restarting.
  bool HasIceCredentials() const { return !ice_credentials_.empty(); }

  // Returns true if `local_description` shares no ICE credentials with the
  // ICE credentials that need restarting.
  bool SatisfiesIceRestart(
      const SessionDescriptionInterface& local_description) const {
    for (const auto& transport_info :
         local_description.description()->transport_infos()) {
      if (ice_credentials_.find(std::make_pair(
              transport_info.description.ice_ufrag,
              transport_info.description.ice_pwd)) != ice_credentials_.end()) {
        return false;
      }
    }
    return true;
  }

 private:
  void AppendIceCredentialsFromSessionDescription(
      const SessionDescriptionInterface& desc) {
    for (const auto& transport_info : desc.description()->transport_infos()) {
      ice_credentials_.insert(
          std::make_pair(transport_info.description.ice_ufrag,
                         transport_info.description.ice_pwd));
    }
  }

  std::set<std::pair<std::string, std::string>> ice_credentials_;
};

SdpOfferAnswerHandler::SdpOfferAnswerHandler(const Environment& env,
                                             PeerConnectionSdpMethods* pc,
                                             ConnectionContext* context)
    : env_(env),
      pc_(pc),
      context_(context),
      local_streams_(StreamCollection::Create()),
      remote_streams_(StreamCollection::Create()),
      operations_chain_(OperationsChain::Create()),
      rtcp_cname_(GenerateRtcpCname()),
      local_ice_credentials_to_replace_(new LocalIceCredentialsToReplace()),
      audio_options_([&]() {
        AudioOptions options;
        const auto& configuration = *pc->configuration();
        options.audio_jitter_buffer_max_packets =
            configuration.audio_jitter_buffer_max_packets;
        options.audio_jitter_buffer_fast_accelerate =
            configuration.audio_jitter_buffer_fast_accelerate;
        options.audio_jitter_buffer_min_delay_ms =
            configuration.audio_jitter_buffer_min_delay_ms;
        return options;
      }()),
      video_options_([&]() {
        VideoOptions options;
        const auto& configuration = *pc->configuration();
        options.screencast_min_bitrate_kbps =
            configuration.screencast_min_bitrate.value_or(100);
        return options;
      }()),
      pt_suggester_(pc_->configuration()->bundle_policy, env_),
      weak_ptr_factory_(this) {
  operations_chain_->SetOnChainEmptyCallback(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr()]() {
        if (!this_weak_ptr)
          return;
        this_weak_ptr->OnOperationsChainEmpty();
      });
}

SdpOfferAnswerHandler::~SdpOfferAnswerHandler() {}

// Static
std::unique_ptr<SdpOfferAnswerHandler> SdpOfferAnswerHandler::Create(
    const Environment& env,
    PeerConnectionSdpMethods* pc,
    std::unique_ptr<RTCCertificateGeneratorInterface> cert_generator,
    std::unique_ptr<VideoBitrateAllocatorFactory>
        video_bitrate_allocator_factory,
    ConnectionContext* context,
    CodecLookupHelper* codec_lookup_helper) {
  auto handler = absl::WrapUnique(new SdpOfferAnswerHandler(env, pc, context));
  handler->Initialize(std::move(cert_generator),
                      std::move(video_bitrate_allocator_factory), context,
                      codec_lookup_helper);
  return handler;
}

void SdpOfferAnswerHandler::Initialize(
    std::unique_ptr<RTCCertificateGeneratorInterface> cert_generator,
    std::unique_ptr<VideoBitrateAllocatorFactory>
        video_bitrate_allocator_factory,
    ConnectionContext* context,
    CodecLookupHelper* codec_lookup_helper) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  const auto& configuration = *pc_->configuration();

  // Obtain a certificate from RTCConfiguration if any were provided (optional).
  scoped_refptr<RTCCertificate> certificate;
  if (!configuration.certificates.empty()) {
    // TODO(hbos,torbjorng): Decide on certificate-selection strategy instead of
    // just picking the first one. The decision should be made based on the DTLS
    // handshake. The DTLS negotiations need to know about all certificates.
    certificate = configuration.certificates[0];
  }

  webrtc_session_desc_factory_ =
      std::make_unique<WebRtcSessionDescriptionFactory>(
          context, this, pc_->session_id(), pc_->dtls_enabled(),
          std::move(cert_generator), std::move(certificate),
          [this](scoped_refptr<RTCCertificate> certificate) {
            RTC_DCHECK_RUN_ON(signaling_thread());
            transport_controller_s()->SetLocalCertificate(certificate);
          },
          codec_lookup_helper, pc_->env());

  if (pc_->options()->disable_encryption) {
    RTC_LOG(LS_INFO)
        << "Disabling encryption. This should only be done in tests.";
    webrtc_session_desc_factory_->SetInsecureForTesting();
  }

  webrtc_session_desc_factory_->set_enable_encrypted_rtp_header_extensions(
      pc_->GetCryptoOptions().srtp.enable_encrypted_rtp_header_extensions);
  webrtc_session_desc_factory_->set_is_unified_plan(IsUnifiedPlan());

  video_bitrate_allocator_factory_ = std::move(video_bitrate_allocator_factory);
  if (!video_bitrate_allocator_factory_) {
    video_bitrate_allocator_factory_ =
        CreateBuiltinVideoBitrateAllocatorFactory();
  }
  codec_lookup_helper_ = codec_lookup_helper;

  max_sctp_streams_ = configuration.max_sctp_streams;
}

// ==================================================================
// Access to pc_ variables
const MediaEngineInterface* SdpOfferAnswerHandler::media_engine() const {
  RTC_DCHECK(context_);
  return context_->media_engine();
}

TransceiverList* SdpOfferAnswerHandler::transceivers() {
  if (!pc_->rtp_manager()) {
    return nullptr;
  }
  return pc_->rtp_manager()->transceivers();
}

const TransceiverList* SdpOfferAnswerHandler::transceivers() const {
  if (!pc_->rtp_manager()) {
    return nullptr;
  }
  return pc_->rtp_manager()->transceivers();
}
JsepTransportController* SdpOfferAnswerHandler::transport_controller_s() {
  return pc_->transport_controller_s();
}
JsepTransportController* SdpOfferAnswerHandler::transport_controller_n() {
  return pc_->transport_controller_n();
}
const JsepTransportController* SdpOfferAnswerHandler::transport_controller_s()
    const {
  return pc_->transport_controller_s();
}
const JsepTransportController* SdpOfferAnswerHandler::transport_controller_n()
    const {
  return pc_->transport_controller_n();
}
DataChannelController* SdpOfferAnswerHandler::data_channel_controller() {
  return pc_->data_channel_controller();
}
const DataChannelController* SdpOfferAnswerHandler::data_channel_controller()
    const {
  return pc_->data_channel_controller();
}
RtpTransmissionManager* SdpOfferAnswerHandler::rtp_manager() {
  return pc_->rtp_manager();
}
const RtpTransmissionManager* SdpOfferAnswerHandler::rtp_manager() const {
  return pc_->rtp_manager();
}

// ===================================================================

void SdpOfferAnswerHandler::PrepareForShutdown() {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void SdpOfferAnswerHandler::UpdateCachedIceCredentials(
    std::vector<IceParameters> credentials) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  ++cached_pooled_ice_credentials_generation_;
  cached_pooled_ice_credentials_ = std::move(credentials);
}

void SdpOfferAnswerHandler::OnPooledIceCredentials(
    uint32_t generation,
    std::vector<IceParameters> credentials) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // If the cache was set or cleared since the MaybeStartGathering() call, these
  // credentials are older than what it holds.
  if (generation == cached_pooled_ice_credentials_generation_) {
    cached_pooled_ice_credentials_ = std::move(credentials);
  }
}

void SdpOfferAnswerHandler::Close() {
  RTC_LOG_THREAD_BLOCK_COUNT();
  ChangeSignalingState(PeerConnectionInterface::kClosed);
}

void SdpOfferAnswerHandler::RestartIce() {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  local_ice_credentials_to_replace_->SetIceCredentialsFromLocalDescriptions(
      current_local_description(), pending_local_description());
  UpdateNegotiationNeeded();
}

Thread* SdpOfferAnswerHandler::signaling_thread() const {
  return context_->signaling_thread();
}

Thread* SdpOfferAnswerHandler::network_thread() const {
  return context_->network_thread();
}

void SdpOfferAnswerHandler::CreateOffer(
    CreateSessionDescriptionObserver* observer,
    const PeerConnectionInterface::RTCOfferAnswerOptions& options) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (auto* tracer = pc_->tracer()) {
    tracer->OnCreateOffer(options);
  }
  // Chain this operation. If asynchronous operations are pending on the chain,
  // this operation will be queued to be invoked, otherwise the contents of the
  // lambda will execute immediately.
  operations_chain_->ChainOperation(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(),
       observer_refptr =
           scoped_refptr<CreateSessionDescriptionObserver>(observer),
       options](std::function<void()> operations_chain_callback) {
        // Abort early if `this_weak_ptr` is no longer valid.
        if (!this_weak_ptr) {
          observer_refptr->OnFailure(
              RTCError(RTCErrorType::INTERNAL_ERROR,
                       "CreateOffer failed because the session was shut down"));
          operations_chain_callback();
          return;
        }
        // The operation completes asynchronously when the wrapper is invoked.
        auto observer_wrapper =
            make_ref_counted<CreateSessionDescriptionObserverOperationWrapper>(
                std::move(observer_refptr),
                std::move(operations_chain_callback), this_weak_ptr,
                SdpType::kOffer);
        if (this_weak_ptr->pc_->IsClosed()) {
          const absl::string_view error =
              "CreateOffer called when PeerConnection is closed.";
          RTC_LOG(LS_ERROR) << error;
          observer_wrapper->OnFailure(
              RTCError(RTCErrorType::INVALID_STATE, error));
          return;
        }
        const auto signaling_state = this_weak_ptr->signaling_state();
        if (options.restrict_offer_to_stable_or_have_local_offer &&
            signaling_state != PeerConnectionInterface::kStable &&
            signaling_state != PeerConnectionInterface::kHaveLocalOffer) {
          const absl::string_view error =
              "PeerConnection cannot create an offer in a state other than "
              "stable or have-local-offer.";
          RTC_LOG(LS_ERROR) << error;
          observer_wrapper->OnFailure(
              RTCError(RTCErrorType::INVALID_STATE, error));
          return;
        }
        this_weak_ptr->DoCreateOffer(options, observer_wrapper);
      });
}

void SdpOfferAnswerHandler::SetLocalDescription(
    scoped_refptr<SetSessionDescriptionObserver> observer,
    std::unique_ptr<SessionDescriptionInterface> desc) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(desc);
  RTC_DCHECK(observer);
  if (auto* tracer = pc_->tracer()) {
    tracer->OnSetLocalDescription(desc.get());
  }
  // Chain this operation. If asynchronous operations are pending on the chain,
  // this operation will be queued to be invoked, otherwise the contents of the
  // lambda will execute immediately.
  operations_chain_->ChainOperation(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(),
       observer_refptr = observer, desc = std::move(desc)](
          std::function<void()> operations_chain_callback) mutable {
        // Abort early if `this_weak_ptr` is no longer valid.
        if (!this_weak_ptr) {
          // For consistency with SetSessionDescriptionObserverAdapter whose
          // posted messages doesn't get processed when the PC is destroyed, we
          // do not inform `observer_refptr` that the operation failed.
          operations_chain_callback();
          return;
        }
        // SetSessionDescriptionObserverAdapter takes care of making sure the
        // `observer_refptr` is invoked in a posted message.
        this_weak_ptr->DoSetLocalDescription(
            std::move(desc),
            make_ref_counted<SetSessionDescriptionObserverAdapter>(
                this_weak_ptr, observer_refptr));
        // For backwards-compatability reasons, we declare the operation as
        // completed here (rather than in a post), so that the operation chain
        // is not blocked by this operation when the observer is invoked. This
        // allows the observer to trigger subsequent offer/answer operations
        // synchronously if the operation chain is now empty.
        operations_chain_callback();
      });
}

void SdpOfferAnswerHandler::SetLocalDescription(
    std::unique_ptr<SessionDescriptionInterface> desc,
    scoped_refptr<SetLocalDescriptionObserverInterface> observer) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(desc);
  RTC_DCHECK(observer);
  if (auto* tracer = pc_->tracer()) {
    tracer->OnSetLocalDescription(desc.get());
  }
  // Chain this operation. If asynchronous operations are pending on the chain,
  // this operation will be queued to be invoked, otherwise the contents of the
  // lambda will execute immediately.
  operations_chain_->ChainOperation(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(), observer,
       desc = std::move(desc)](
          std::function<void()> operations_chain_callback) mutable {
        // Abort early if `this_weak_ptr` is no longer valid.
        if (!this_weak_ptr) {
          observer->OnSetLocalDescriptionComplete(RTCError(
              RTCErrorType::INTERNAL_ERROR,
              "SetLocalDescription failed because the session was shut down"));
          observer->OnOperationComplete(std::move(operations_chain_callback));
          return;
        }
        this_weak_ptr->DoSetLocalDescription(std::move(desc), observer);
        observer->OnOperationComplete(std::move(operations_chain_callback));
      });
}

void SdpOfferAnswerHandler::SetLocalDescription(
    scoped_refptr<SetSessionDescriptionObserver> observer) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  scoped_refptr<SetLocalDescriptionObserverInterface> adapter =
      make_ref_counted<SetSessionDescriptionObserverAdapter>(
          weak_ptr_factory_.GetWeakPtr(), std::move(observer));
  SetLocalDescription(std::move(adapter));
}

void SdpOfferAnswerHandler::SetLocalDescription(
    scoped_refptr<SetLocalDescriptionObserverInterface> observer) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(observer);
  if (auto* tracer = pc_->tracer()) {
    tracer->OnSetLocalDescription(nullptr);
  }
  // The `create_sdp_observer` handles performing DoSetLocalDescription() with
  // the resulting description as well as completing the operation.
  auto create_sdp_observer =
      make_ref_counted<ImplicitCreateSessionDescriptionObserver>(
          weak_ptr_factory_.GetWeakPtr(), observer);
  // Chain this operation. If asynchronous operations are pending on the chain,
  // this operation will be queued to be invoked, otherwise the contents of the
  // lambda will execute immediately.
  operations_chain_->ChainOperation(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(),
       create_sdp_observer](std::function<void()> operations_chain_callback) {
        // The `create_sdp_observer` is responsible for completing the
        // operation.
        create_sdp_observer->SetOperationCompleteCallback(
            std::move(operations_chain_callback));
        // Abort early if `this_weak_ptr` is no longer valid. This triggers the
        // same code path as if DoCreateOffer() or DoCreateAnswer() failed.
        SdpOfferAnswerHandler* handler = this_weak_ptr.get();
        if (handler == nullptr) {
          create_sdp_observer->OnFailure(RTCError(
              RTCErrorType::INTERNAL_ERROR,
              "SetLocalDescription failed because the session was shut down"));
          return;
        }
        RTC_DCHECK_RUN_ON(handler->signaling_thread());
        switch (handler->signaling_state()) {
          case PeerConnectionInterface::kStable:
          case PeerConnectionInterface::kHaveLocalOffer:
          case PeerConnectionInterface::kHaveRemotePrAnswer:
            if (handler->last_created_offer_ != nullptr &&
                handler->last_created_offer_version_ ==
                    handler->state_version_) {
              std::unique_ptr<SessionDescriptionInterface> desc_copy =
                  handler->last_created_offer_->Clone();
#if RTC_DCHECK_IS_ON
              handler->VerifyCachedOffer(
                  [create_sdp_observer,
                   desc_copy = std::move(desc_copy)]() mutable {
                    create_sdp_observer->OnSuccess(desc_copy.release());
                  });
#else
              create_sdp_observer->OnSuccess(desc_copy.release());
#endif
            } else {
              handler->DoCreateOffer(
                  PeerConnectionInterface::RTCOfferAnswerOptions(),
                  create_sdp_observer);
            }
            break;
          case PeerConnectionInterface::kHaveLocalPrAnswer:
          case PeerConnectionInterface::kHaveRemoteOffer:
            if (handler->last_created_answer_ != nullptr &&
                handler->last_created_answer_version_ ==
                    handler->state_version_) {
              std::unique_ptr<SessionDescriptionInterface> desc_copy =
                  handler->last_created_answer_->Clone();
#if RTC_DCHECK_IS_ON
              handler->VerifyCachedAnswer(
                  [create_sdp_observer,
                   desc_copy = std::move(desc_copy)]() mutable {
                    create_sdp_observer->OnSuccess(desc_copy.release());
                  });
#else
              create_sdp_observer->OnSuccess(desc_copy.release());
#endif
            } else {
              handler->DoCreateAnswer(
                  PeerConnectionInterface::RTCOfferAnswerOptions(),
                  create_sdp_observer);
            }
            break;
          case PeerConnectionInterface::kClosed:
            create_sdp_observer->OnFailure(RTCError(
                RTCErrorType::INVALID_STATE,
                "SetLocalDescription called when PeerConnection is closed."));
            break;
        }
      });
}

RTCError SdpOfferAnswerHandler::ApplyLocalDescription(
    std::unique_ptr<SessionDescriptionInterface> desc,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::ApplyLocalDescription");
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(desc);

  // Invalidate the stats caches to make sure that they get
  // updated the next time getStats() gets called, as updating the session
  // description affects the stats.
  pc_->ClearStatsCache();

  // Take a reference to the old local description since it's used below to
  // compare against the new local description. When setting the new local
  // description, grab ownership of the replaced session description in case it
  // is the same as `old_local_description`, to keep it alive for the duration
  // of the method.
  const SessionDescriptionInterface* old_local_description =
      local_description();
  std::unique_ptr<SessionDescriptionInterface> replaced_local_description;
  SdpType type = desc->GetType();
  if (type == SdpType::kAnswer) {
    replaced_local_description = pending_local_description_
                                     ? std::move(pending_local_description_)
                                     : std::move(current_local_description_);
    current_local_description_ = std::move(desc);
    pending_local_description_ = nullptr;
    current_remote_description_ = std::move(pending_remote_description_);
  } else {
    replaced_local_description = std::move(pending_local_description_);
    pending_local_description_ = std::move(desc);
  }
  if (!initial_offerer_) {
    initial_offerer_.emplace(type == SdpType::kOffer);
  }
  // The session description to apply now must be accessed by
  // `local_description()`.
  RTC_DCHECK(local_description());

  if (!is_caller_) {
    if (remote_description()) {
      // Remote description was applied first, so this PC is the callee.
      is_caller_ = false;
    } else {
      // Local description is applied first, so this PC is the caller.
      is_caller_ = true;
    }
  }

  RTCError error = PushdownTransportDescription(CS_LOCAL, type);
  if (!error.ok()) {
    return error;
  }

  if (IsUnifiedPlan()) {
    error = UpdateTransceiversAndDataChannels(
        CS_LOCAL, *local_description(), old_local_description,
        remote_description(), bundle_groups_by_mid);
    if (!error.ok()) {
      RTC_LOG(LS_ERROR) << error.message() << " (" << type << ")";
      return error;
    }
    if (ConfiguredForMedia()) {
      std::vector<scoped_refptr<RtpTransceiverInterface>> remove_list;
      std::vector<scoped_refptr<MediaStreamInterface>> removed_streams;
      flat_map<std::string, DtlsTransportAndName> dtls_transports_by_mid =
          GetDtlsTransports(*transceivers(), transport_controller_s());

      for (const auto& transceiver_ext : transceivers()->List()) {
        auto transceiver = transceiver_ext->internal();
        if (transceiver->stopped()) {
          continue;
        }

        // 2.2.7.1.1.(6-9): Set sender and receiver's transport slots.
        // Note that code paths that don't set MID won't be able to use
        // information about DTLS transports.
        if (transceiver->mid()) {
          auto it = dtls_transports_by_mid.find(*transceiver->mid());
          RTC_DCHECK(it != dtls_transports_by_mid.end());
          transceiver->SetTransport(it->second.transport,
                                    it->second.transport_name);
        }

        const ContentInfo* content =
            FindMediaSectionForTransceiver(transceiver, local_description());
        if (!content) {
          continue;
        }
        const MediaContentDescription* media_desc =
            content->media_description();
        // 2.2.7.1.6: If description is of type "answer" or "pranswer", then run
        // the following steps:
        if (type == SdpType::kPrAnswer || type == SdpType::kAnswer) {
          // 2.2.7.1.6.1: If direction is "sendonly" or "inactive", and
          // transceiver's [[FiredDirection]] slot is either "sendrecv" or
          // "recvonly", process the removal of a remote track for the media
          // description, given transceiver, removeList, and muteTracks.
          if (!RtpTransceiverDirectionHasRecv(media_desc->direction()) &&
              (transceiver->fired_direction() &&
               RtpTransceiverDirectionHasRecv(
                   *transceiver->fired_direction()))) {
            ProcessRemovalOfRemoteTrack(transceiver_ext, &remove_list,
                                        &removed_streams);
          }
          // 2.2.7.1.6.2: Set transceiver's [[CurrentDirection]] and
          // [[FiredDirection]] slots to direction.
          transceiver->set_current_direction(media_desc->direction());
          transceiver->set_fired_direction(media_desc->direction());
        }
        transceiver->set_receptive(
            RtpTransceiverDirectionHasRecv(media_desc->direction()));
        transceiver->ApplySframeEnabled(media_desc->sframe_enabled());
      }
      pc_->RunWithObserver([&](auto observer) {
        for (const auto& transceiver : remove_list) {
          observer->OnRemoveTrack(transceiver->receiver());
        }
        for (const auto& stream : removed_streams) {
          observer->OnRemoveStream(stream);
        }
      });
    }
  } else {
    // Media channels will be created only when offer is set. These may use new
    // transports just created by PushdownTransportDescription.
    if (type == SdpType::kOffer) {
      // TODO(bugs.webrtc.org/4676) - Handle CreateChannel failure, as new local
      // description is applied. Restore back to old description.
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
      error = CreateChannels(*local_description()->description());
      RTC_ALLOW_PLAN_B_DEPRECATION_END();
      if (!error.ok()) {
        RTC_LOG(LS_ERROR) << error.message() << " (" << type << ")";
        return error;
      }
    }
    // Plan B transport synchronization.
    flat_map<std::string, DtlsTransportAndName> dtls_transports_by_mid =
        GetDtlsTransports(*transceivers(), transport_controller_s());
    for (const auto& transceiver_ext : transceivers()->List()) {
      auto transceiver = transceiver_ext->internal();
      if (transceiver->mid()) {
        auto it = dtls_transports_by_mid.find(*transceiver->mid());
        if (it != dtls_transports_by_mid.end()) {
          transceiver->SetTransport(it->second.transport,
                                    it->second.transport_name);
        }
      }
    }
    // Remove unused channels if MediaContentDescription is rejected.
    RemoveUnusedChannels(local_description()->description());
  }

  error = UpdateSessionState(type, CS_LOCAL, local_description()->description(),
                             bundle_groups_by_mid);
  if (!error.ok()) {
    return RTC_LOG_ERROR(error);
  }

  // Now that we have a local description, we can push down remote candidates.
  UseCandidatesInRemoteDescription();

  pending_ice_restarts_.clear();
  if (session_error() != SessionError::kNone) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << GetSessionErrorMsg());
  }

  // Validate SSRCs, we do not allow duplicates.
  if (ConfiguredForMedia()) {
    flat_set<uint32_t> used_ssrcs;
    for (const auto& content : local_description()->description()->contents()) {
      for (const auto& stream : content.media_description()->streams()) {
        for (uint32_t ssrc : stream.ssrcs) {
          auto result = used_ssrcs.insert(ssrc);
          if (!result.second) {
            return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                                 << "Duplicate ssrc " << ssrc
                                 << " is not allowed");
          }
        }
      }
    }
  }

  if (IsUnifiedPlan()) {
    UpdateSenderSsrcsFromLocalDescription();
  } else {
    // Plan B semantics.

    // Update state and SSRC of local MediaStreams and DataChannels based on the
    // local session description.
    const ContentInfo* audio_content =
        GetFirstAudioContent(local_description()->description());
    if (audio_content) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
      if (audio_content->rejected) {
        RemoveSenders(MediaType::AUDIO);
      } else {
        const MediaContentDescription* audio_desc =
            audio_content->media_description();
        UpdateLocalSendersPlanB(audio_desc->streams(), audio_desc->type());
      }
      RTC_ALLOW_PLAN_B_DEPRECATION_END();
    }

    const ContentInfo* video_content =
        GetFirstVideoContent(local_description()->description());
    if (video_content) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
      if (video_content->rejected) {
        RemoveSenders(MediaType::VIDEO);
      } else {
        const MediaContentDescription* video_desc =
            video_content->media_description();
        UpdateLocalSendersPlanB(video_desc->streams(), video_desc->type());
      }
      RTC_ALLOW_PLAN_B_DEPRECATION_END();
    }
  }

  // This function does nothing with data content.

  if (type == SdpType::kAnswer &&
      local_ice_credentials_to_replace_->SatisfiesIceRestart(
          *current_local_description_)) {
    local_ice_credentials_to_replace_->ClearIceCredentials();
  }

  if (IsUnifiedPlan()) {
    UpdateRtpHeaderExtensionPreferencesFromSdpMunging(
        local_description()->description(), transceivers());
  }

  return RTCError::OK();
}

void SdpOfferAnswerHandler::UpdateSenderSsrcsFromLocalDescription() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(IsUnifiedPlan());
  if (!ConfiguredForMedia()) {
    return;
  }
  ScopedOperationsBatcher worker_tasks(context_->worker_thread());
  // We must use List and not ListInternal here because
  // transceivers()->StableState() is indexed by the non-internal refptr.
  for (const auto& transceiver_ext : transceivers()->List()) {
    RtpTransceiver* transceiver = transceiver_ext->internal();
    if (transceiver->stopped()) {
      continue;
    }
    const ContentInfo* content =
        FindMediaSectionForTransceiver(transceiver, local_description());
    if (content == nullptr) {
      continue;
    }
    scoped_refptr<RtpSenderInternal> sender = transceiver->sender_internal();
    if (content->rejected || !transceiver->HasChannel() ||
        transceiver->channel_local_streams().empty()) {
      // 0 is a special value meaning "this sender has no associated send
      // stream". Need to call this so the sender won't attempt to configure
      // a no longer existing stream and run into DCHECKs in the lower
      // layers.
      worker_tasks.AddWithFinalizer(sender->SetSsrcTask(0, /*layer_count=*/0));
    } else {
      const std::vector<StreamParams>& streams =
          transceiver->channel_local_streams();
      sender->set_stream_ids(streams[0].stream_ids());
      std::vector<RtpEncodingParameters> encodings =
          sender->init_send_encodings();
      // The local description is authoritative for the number of send layers.
      // This is the same view that the media channel has, since the channel
      // derives its encodings from these same `StreamParams` (see
      // `CreateRtpParametersWithEncodings()`). Computing it here means the
      // sender does not have to discover a mismatch on the worker thread,
      // where the context needed to act on it is gone.
      std::vector<uint32_t> primary_ssrcs;
      streams[0].GetPrimarySsrcs(&primary_ssrcs);
      worker_tasks.AddWithFinalizer(
          sender->SetSsrcTask(streams[0].first_ssrc(), primary_ssrcs.size()));
      if (!encodings.empty()) {
        transceivers()
            ->StableState(transceiver_ext)
            ->SetInitSendEncodings(std::move(encodings));
      }
    }
  }
}

void SdpOfferAnswerHandler::SetRemoteDescription(
    scoped_refptr<SetSessionDescriptionObserver> observer,
    std::unique_ptr<SessionDescriptionInterface> desc) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(observer);
  RTC_DCHECK(desc);
  SetRemoteDescription(
      std::move(desc),
      make_ref_counted<SetSessionDescriptionObserverAdapter>(
          weak_ptr_factory_.GetWeakPtr(), std::move(observer)));
}

void SdpOfferAnswerHandler::SetRemoteDescription(
    std::unique_ptr<SessionDescriptionInterface> desc,
    scoped_refptr<SetRemoteDescriptionObserverInterface> observer) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(desc);
  RTC_DCHECK(observer);
  if (auto* tracer = pc_->tracer()) {
    tracer->OnSetRemoteDescription(desc.get());
  }
  // Chain this operation. If asynchronous operations are pending on the chain,
  // this operation will be queued to be invoked, otherwise the contents of the
  // lambda will execute immediately.
  operations_chain_->ChainOperation(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(), observer,
       desc = std::move(desc)](
          std::function<void()> operations_chain_callback) mutable {
        // Abort early if `this_weak_ptr` is no longer valid.
        if (!this_weak_ptr) {
          observer->OnSetRemoteDescriptionComplete(RTCError(
              RTCErrorType::INTERNAL_ERROR,
              "SetRemoteDescription failed because the session was shut down"));
          operations_chain_callback();
          return;
        }

        this_weak_ptr->DoSetRemoteDescription(
            std::make_unique<RemoteDescriptionOperation>(
                this_weak_ptr.get(), std::move(desc), std::move(observer),
                std::move(operations_chain_callback)));
      });
}

RTCError SdpOfferAnswerHandler::ReplaceRemoteDescription(
    std::unique_ptr<SessionDescriptionInterface> desc,
    SdpType sdp_type,
    std::unique_ptr<SessionDescriptionInterface>* replaced_description) {
  RTC_DCHECK(replaced_description);
  if (sdp_type == SdpType::kAnswer) {
    *replaced_description = pending_remote_description_
                                ? std::move(pending_remote_description_)
                                : std::move(current_remote_description_);
    current_remote_description_ = std::move(desc);
    pending_remote_description_ = nullptr;
    current_local_description_ = std::move(pending_local_description_);
  } else {
    *replaced_description = std::move(pending_remote_description_);
    pending_remote_description_ = std::move(desc);
  }

  // The session description to apply now must be accessed by
  // `remote_description()`.
  const SessionDescription* session_desc = remote_description()->description();

  const auto* local = local_description();

  // NOTE: This will perform a BlockingCall() to the network thread.
  return transport_controller_s()->SetRemoteDescription(
      sdp_type, local ? local->description() : nullptr, session_desc);
}

void SdpOfferAnswerHandler::ApplyRemoteDescription(
    std::unique_ptr<RemoteDescriptionOperation> operation) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::ApplyRemoteDescription");
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(operation->description());

  // Invalidate the stats caches to make sure that they get
  // updated next time getStats() gets called, as updating the session
  // description affects the stats.
  pc_->ClearStatsCache();

  if (!operation->ReplaceRemoteDescriptionAndCheckError())
    return;

  if (!operation->UpdateChannels())
    return;

  // NOTE: Candidates allocation will be initiated only when
  // SetLocalDescription is called.
  if (!operation->UpdateSessionState())
    return;

  if (!operation->UseCandidatesInRemoteDescription())
    return;

  if (operation->old_remote_description()) {
    for (const ContentInfo& content :
         operation->old_remote_description()->description()->contents()) {
      // Check if this new SessionDescription contains new ICE ufrag and
      // password that indicates the remote peer requests an ICE restart.
      // TODO(deadbeef): When we start storing both the current and pending
      // remote description, this should reset pending_ice_restarts and compare
      // against the current description.
      if (CheckForRemoteIceRestart(operation->old_remote_description(),
                                   remote_description(), content.mid())) {
        if (operation->type() == SdpType::kOffer) {
          pending_ice_restarts_.insert(std::string(content.mid()));
        }
      } else {
        // We retain all received candidates only if ICE is not restarted.
        // When ICE is restarted, all previous candidates belong to an old
        // generation and should not be kept.
        // TODO(deadbeef): This goes against the W3C spec which says the remote
        // description should only contain candidates from the last set remote
        // description plus any candidates added since then. We should remove
        // this once we're sure it won't break anything.
        WebRtcSessionDescriptionFactory::CopyCandidatesFromSessionDescription(
            operation->old_remote_description(), content.mid(),
            mutable_remote_description());
      }
    }
  }

  if (operation->HaveSessionError())
    return;

  // We have now decided that the operation will be applied.
  pt_suggester_.Update(remote_description()->description(),
                       /* local= */ false, remote_description()->GetType());

  // Set the the ICE connection state to connecting since the connection may
  // become writable with peer reflexive candidates before any remote candidate
  // is signaled.
  // TODO(pthatcher): This is a short-term solution for crbug/446908. A real fix
  // is to have a new signal the indicates a change in checking state from the
  // transport and expose a new checking() member from transport that can be
  // read to determine the current checking state. The existing SignalConnecting
  // actually means "gathering candidates", so cannot be be used here.
  if (remote_description()->GetType() != SdpType::kOffer &&
      remote_description()->number_of_mediasections() > 0u &&
      pc_->ice_connection_state_internal() ==
          PeerConnectionInterface::kIceConnectionNew) {
    pc_->SetIceConnectionState(PeerConnectionInterface::kIceConnectionChecking);
  }

  if (operation->unified_plan()) {
    ApplyRemoteDescriptionUpdateTransceiverState(operation->type());
  }
  remote_peer_supports_msid_ =
      remote_description()->description()->msid_signaling() !=
      kMsidSignalingNotUsed;

  if (!operation->unified_plan()) {
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    PlanBUpdateSendersAndReceivers(
        GetFirstAudioContent(remote_description()->description()),
        GetFirstAudioContentDescription(remote_description()->description()),
        GetFirstVideoContent(remote_description()->description()),
        GetFirstVideoContentDescription(remote_description()->description()));
    RTC_ALLOW_PLAN_B_DEPRECATION_END();

    // Plan B transport synchronization.
    flat_map<std::string, DtlsTransportAndName> dtls_transports_by_mid =
        GetDtlsTransports(*transceivers(), transport_controller_s());
    for (const auto& transceiver_ext : transceivers()->List()) {
      auto transceiver = transceiver_ext->internal();
      if (transceiver->mid()) {
        auto it = dtls_transports_by_mid.find(*transceiver->mid());
        if (it != dtls_transports_by_mid.end()) {
          transceiver->SetTransport(it->second.transport,
                                    it->second.transport_name);
        }
      }
    }
  }

  if (operation->type() == SdpType::kAnswer) {
    if (local_ice_credentials_to_replace_->SatisfiesIceRestart(
            *current_local_description_)) {
      local_ice_credentials_to_replace_->ClearIceCredentials();
    }

    RemoveStoppedTransceivers();
  }

  // Consider the operation complete at this point.
  operation->SignalCompletion();

  // The observer may have closed the PeerConnection. If so, the transport
  // controller and the port allocator are gone and there is nothing left to do.
  if (pc_->IsClosed()) {
    return;
  }

  SetRemoteDescriptionPostProcess(operation->type() == SdpType::kAnswer);
}

void SdpOfferAnswerHandler::ApplyRemoteDescriptionUpdateTransceiverState(
    SdpType sdp_type) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(IsUnifiedPlan());
  if (!ConfiguredForMedia()) {
    return;
  }
  std::vector<scoped_refptr<RtpTransceiverInterface>>
      now_receiving_transceivers;
  std::vector<scoped_refptr<RtpTransceiverInterface>> remove_list;
  std::vector<scoped_refptr<MediaStreamInterface>> added_streams;
  std::vector<scoped_refptr<MediaStreamInterface>> removed_streams;
  ScopedOperationsBatcher network_tasks(context_->network_thread());
  ScopedOperationsBatcher worker_tasks(context_->worker_thread());
  flat_map<std::string, DtlsTransportAndName> dtls_transports_by_mid =
      GetDtlsTransports(*transceivers(), transport_controller_s());

  for (const auto& transceiver_ext : transceivers()->List()) {
    const auto transceiver = transceiver_ext->internal();
    const ContentInfo* content =
        FindMediaSectionForTransceiver(transceiver, remote_description());
    if (!content) {
      continue;
    }
    const MediaContentDescription* media_desc = content->media_description();
    RtpTransceiverDirection local_direction =
        RtpTransceiverDirectionReversed(media_desc->direction());
    // Remember the previous remote streams if this is a remote offer. This
    // makes it possible to rollback modifications to the streams.
    if (sdp_type == SdpType::kOffer) {
      transceivers()
          ->StableState(transceiver_ext)
          ->SetRemoteStreamIds(transceiver->receiver()->stream_ids());
    }
    // Roughly the same as steps 2.2.8.6 of section 4.4.1.6 "Set the
    // RTCSessionDescription: Set the associated remote streams given
    // transceiver.[[Receiver]], msids, addList, and removeList".
    // https://w3c.github.io/webrtc-pc/#set-the-rtcsessiondescription
    if (RtpTransceiverDirectionHasRecv(local_direction)) {
      std::vector<std::string> stream_ids;
      if (!media_desc->streams().empty()) {
        // The remote description has signaled the stream IDs.
        stream_ids = media_desc->streams()[0].stream_ids();
      }

      RTC_LOG(LS_INFO) << "Processing the MSIDs for MID=" << content->mid()
                       << " (" << GetStreamIdsString(stream_ids) << ").";
      SetAssociatedRemoteStreams(transceiver->receiver_internal(), stream_ids,
                                 &added_streams, &removed_streams);
      // From the WebRTC specification, steps 2.2.8.5/6 of section 4.4.1.6
      // "Set the RTCSessionDescription: If direction is sendrecv or recvonly,
      // and transceiver's current direction is neither sendrecv nor recvonly,
      // process the addition of a remote track for the media description.
      if (!transceiver->fired_direction() ||
          !RtpTransceiverDirectionHasRecv(*transceiver->fired_direction())) {
        RTC_LOG(LS_INFO) << "Processing the addition of a remote track for MID="
                         << content->mid() << ".";
        // Since the transceiver is passed to the user in an
        // OnTrack event, we must use the proxied transceiver.
        now_receiving_transceivers.push_back(transceiver_ext);
      }
    } else {
      transceiver->set_receptive(false);
    }
    // 2.2.8.1.9: If direction is "sendonly" or "inactive", and transceiver's
    // [[FiredDirection]] slot is either "sendrecv" or "recvonly", process the
    // removal of a remote track for the media description, given transceiver,
    // removeList, and muteTracks.
    if (!RtpTransceiverDirectionHasRecv(local_direction) &&
        (transceiver->fired_direction() &&
         RtpTransceiverDirectionHasRecv(*transceiver->fired_direction()))) {
      ProcessRemovalOfRemoteTrack(transceiver_ext, &remove_list,
                                  &removed_streams);
    }
    // 2.2.8.1.10: Set transceiver's [[FiredDirection]] slot to direction.
    if (sdp_type == SdpType::kOffer) {
      // Remember the previous fired direction if this is a remote offer. This
      // makes it possible to rollback modifications to [[FiredDirection]],
      // which is necessary for "ontrack" to fire in or after rollback.
      transceivers()
          ->StableState(transceiver_ext)
          ->SetFiredDirection(transceiver->fired_direction());
    }
    transceiver->set_fired_direction(local_direction);
    // 2.2.8.1.11: If description is of type "answer" or "pranswer", then run
    // the following steps:
    if (sdp_type == SdpType::kPrAnswer || sdp_type == SdpType::kAnswer) {
      // 2.2.8.1.11.1: Set transceiver's [[CurrentDirection]] slot to
      // direction.
      transceiver->set_current_direction(local_direction);
      // 2.2.8.1.11.[3-6]: Set the transport internal slots.
      // A stopped transceiver has no channel and therefore no transport.
      if (transceiver->mid() && !transceiver->stopped()) {
        auto it = dtls_transports_by_mid.find(*transceiver->mid());
        RTC_DCHECK(it != dtls_transports_by_mid.end());
        transceiver->SetTransport(it->second.transport,
                                  it->second.transport_name);
      }
    }
    // 2.2.8.1.12: If the media description is rejected, and transceiver is
    // not already stopped, stop the RTCRtpTransceiver transceiver.
    if (content->rejected && !transceiver->stopped()) {
      RTC_LOG(LS_INFO) << "Stopping transceiver for MID=" << content->mid()
                       << " since the media section was rejected.";
      worker_tasks.Add(transceiver->GetStopTransceiverProcedure());
    }
    // If the local offer included Sframe but the remote answer does not,
    // stop the transceiver since Sframe cannot be downgraded.
    if (sdp_type == SdpType::kPrAnswer || sdp_type == SdpType::kAnswer) {
      const ContentInfo* local_content =
          FindMediaSectionForTransceiver(transceiver, local_description());
      if (local_content && !content->rejected &&
          local_content->media_description()->sframe_enabled() &&
          !media_desc->sframe_enabled() && !transceiver->stopped()) {
        RTC_LOG(LS_INFO) << "Stopping transceiver for MID=" << content->mid()
                         << " since the remote answer does not include Sframe.";
        network_tasks.Add(transceiver->GetClearChannelNetworkTask());
        worker_tasks.Add(transceiver->GetDeleteChannelWorkerTask(
            /*stop_senders=*/false));
        worker_tasks.Add(transceiver->GetStopTransceiverProcedure());
      }
    }
    if (!content->rejected && !transceiver->stopped() &&
        RtpTransceiverDirectionHasRecv(local_direction)) {
      if (!media_desc->streams().empty() &&
          media_desc->streams()[0].has_ssrcs()) {
        uint32_t ssrc = media_desc->streams()[0].first_ssrc();
        worker_tasks.Add(
            transceiver->receiver_internal()->GetSetupForMediaChannel(ssrc));
      } else {
        worker_tasks.Add(transceiver->receiver_internal()
                             ->GetSetupForUnsignaledMediaChannel());
      }
    }
  }

  RTCError error = network_tasks.Run();
  RTC_DCHECK(error.ok());
  error = worker_tasks.Run();
  RTC_DCHECK(error.ok());

  if (auto* tracer = pc_->tracer()) {
    for (const auto& transceiver : now_receiving_transceivers) {
      tracer->OnTrack(*transceiver);
    }
  }

  // Once all processing has finished, fire off callbacks.
  pc_->RunWithObserver([&](auto observer) {
    for (const auto& transceiver : now_receiving_transceivers) {
      pc_->legacy_stats()->AddTrack(transceiver->receiver()->track().get());
      observer->OnTrack(transceiver);
      observer->OnAddTrack(transceiver->receiver(),
                           transceiver->receiver()->streams());
    }
    for (const auto& stream : added_streams) {
      observer->OnAddStream(stream);
    }
    for (const auto& transceiver : remove_list) {
      observer->OnRemoveTrack(transceiver->receiver());
    }
    for (const auto& stream : removed_streams) {
      observer->OnRemoveStream(stream);
    }
  });
}

void SdpOfferAnswerHandler::PlanBUpdateSendersAndReceivers(
    const ContentInfo* audio_content,
    const AudioContentDescription* audio_desc,
    const ContentInfo* video_content,
    const VideoContentDescription* video_desc) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());

  // We wait to signal new streams until we finish processing the description,
  // since only at that point will new streams have all their tracks.
  scoped_refptr<StreamCollection> new_streams(StreamCollection::Create());

  // TODO(steveanton): When removing RTP senders/receivers in response to a
  // rejected media section, there is some cleanup logic that expects the
  // voice/ video channel to still be set. But in this method the voice/video
  // channel would have been destroyed by the SetRemoteDescription caller
  // above so the cleanup that relies on them fails to run. The RemoveSenders
  // calls should be moved to right before the DestroyChannel calls to fix
  // this.

  // Find all audio rtp streams and create corresponding remote AudioTracks
  // and MediaStreams.
  if (audio_content) {
    if (audio_content->rejected) {
      RemoveSenders(MediaType::AUDIO);
    } else {
      bool default_audio_track_needed =
          !remote_peer_supports_msid_ &&
          RtpTransceiverDirectionHasSend(audio_desc->direction());
      UpdateRemoteSendersListPlanB(GetActiveStreams(audio_desc),
                                   default_audio_track_needed,
                                   audio_desc->type(), new_streams.get());
    }
  }

  // Find all video rtp streams and create corresponding remote VideoTracks
  // and MediaStreams.
  if (video_content) {
    if (video_content->rejected) {
      RemoveSenders(MediaType::VIDEO);
    } else {
      bool default_video_track_needed =
          !remote_peer_supports_msid_ &&
          RtpTransceiverDirectionHasSend(video_desc->direction());
      UpdateRemoteSendersListPlanB(GetActiveStreams(video_desc),
                                   default_video_track_needed,
                                   video_desc->type(), new_streams.get());
    }
  }

  // Iterate new_streams and notify the observer about new MediaStreams.
  pc_->RunWithObserver([&](auto observer) {
    for (size_t i = 0; i < new_streams->count(); ++i) {
      MediaStreamInterface* new_stream = new_streams->at(i);
      pc_->legacy_stats()->AddStream(new_stream);
      observer->OnAddStream(scoped_refptr<MediaStreamInterface>(new_stream));
    }
  });

  UpdateEndedRemoteMediaStreams();
}

void SdpOfferAnswerHandler::ReportInitialSdpMunging(bool had_local_description,
                                                    SdpType type) {
  // Report SDP munging of the initial call to setLocalDescription separately.
  if (!had_local_description) {
    switch (type) {
      case SdpType::kOffer:
        RTC_HISTOGRAM_ENUMERATION(
            "WebRTC.PeerConnection.SdpMunging.Offer.Initial",
            last_sdp_munging_type_, SdpMungingType::kMaxValue);
        break;
      case SdpType::kAnswer:
        RTC_HISTOGRAM_ENUMERATION(
            "WebRTC.PeerConnection.SdpMunging.Answer.Initial",
            last_sdp_munging_type_, SdpMungingType::kMaxValue);
        break;
      case SdpType::kPrAnswer:
        RTC_HISTOGRAM_ENUMERATION(
            "WebRTC.PeerConnection.SdpMunging.PrAnswer.Initial",
            last_sdp_munging_type_, SdpMungingType::kMaxValue);
        break;
      case SdpType::kRollback:
        // Rollback does not have SDP so can not be munged.
        break;
    }
  }
}

void SdpOfferAnswerHandler::TraceCreateSessionDescriptionComplete(
    SdpType type,
    const SessionDescriptionInterface* desc,
    const RTCError& error) {
  auto* tracer = pc_->tracer();
  if (!tracer)
    return;
  if (error.ok()) {
    if (type == SdpType::kOffer) {
      tracer->OnCreateOfferSuccess(desc);
    } else {
      tracer->OnCreateAnswerSuccess(desc);
    }
  } else {
    if (type == SdpType::kOffer) {
      tracer->OnCreateOfferFailure(error);
    } else {
      tracer->OnCreateAnswerFailure(error);
    }
  }
}

void SdpOfferAnswerHandler::TraceSetLocalDescriptionComplete(
    const RTCError& error) {
  auto* tracer = pc_->tracer();
  if (!tracer)
    return;
  if (error.ok()) {
    tracer->OnSetLocalDescriptionSuccess(local_description());
  } else {
    tracer->OnSetLocalDescriptionFailure(error);
  }
}

void SdpOfferAnswerHandler::TraceSetRemoteDescriptionComplete(
    const RTCError& error) {
  auto* tracer = pc_->tracer();
  if (!tracer)
    return;
  if (error.ok()) {
    tracer->OnSetRemoteDescriptionSuccess();
  } else {
    tracer->OnSetRemoteDescriptionFailure(error);
  }
}

void SdpOfferAnswerHandler::DoSetLocalDescription(
    std::unique_ptr<SessionDescriptionInterface> desc,
    scoped_refptr<SetLocalDescriptionObserverInterface> observer) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::DoSetLocalDescription");
  RTC_DCHECK(desc);
  RTC_DCHECK(observer);

  // If a session error has occurred the PeerConnection is in a possibly
  // inconsistent state so fail right away.
  if (session_error() != SessionError::kNone) {
    std::string error_message = GetSessionErrorMsg();
    RTC_LOG(LS_ERROR) << "SetLocalDescription: " << error_message;
    RTCError error(RTCErrorType::INTERNAL_ERROR, std::move(error_message));
    TraceSetLocalDescriptionComplete(error);
    observer->OnSetLocalDescriptionComplete(std::move(error));
    return;
  }

  // For SLD we support only explicit rollback.
  if (desc->GetType() == SdpType::kRollback) {
    if (IsUnifiedPlan()) {
      RTCError error = Rollback(desc->GetType());
      TraceSetLocalDescriptionComplete(error);
      observer->OnSetLocalDescriptionComplete(std::move(error));
    } else {
      RTCError error(RTCErrorType::UNSUPPORTED_OPERATION,
                     "Rollback not supported in Plan B");
      TraceSetLocalDescriptionComplete(error);
      observer->OnSetLocalDescriptionComplete(std::move(error));
    }
    return;
  }

  flat_map<std::string, const ContentGroup*> bundle_groups_by_mid =
      GetBundleGroupsByMid(desc->description());
  RTCError error =
      ValidateSessionDescription(desc.get(), CS_LOCAL, bundle_groups_by_mid);
  if (!error.ok()) {
    std::string error_message =
        GetSetDescriptionErrorMessage(CS_LOCAL, desc->GetType(), error);
    RTC_LOG(LS_ERROR) << error_message;
    error = RTCError(error.type(), std::move(error_message));
    TraceSetLocalDescriptionComplete(error);
    observer->OnSetLocalDescriptionComplete(std::move(error));
    return;
  }

  SessionDescriptionInterface* last_created_desc =
      desc->GetType() == SdpType::kOffer ? last_created_offer_.get()
                                         : last_created_answer_.get();

  // Determine if SDP munging was done. This is not yet acted upon.
  bool had_local_description = !!local_description();
  SdpMungingType sdp_munging_type =
      DetermineSdpMungingType(desc.get(), last_created_desc);

  if (!disable_sdp_munging_checks_) {
    bool reject_with_error =
        !IsSdpMungingAllowed(sdp_munging_type, pc_->trials());
    if (!reject_with_error &&
        HasUfragSdpMunging(desc.get(), last_created_desc)) {
      // ice-ufrag munging may still trigger rejection for other reasons
      // guarded by this variable.
      has_sdp_munged_ufrag_ = true;

      if (pc_->trials().IsEnabled("WebRTC-NoSdpMangleUfrag")) {
        RTC_LOG(LS_ERROR) << "Rejecting SDP because of ufrag modification";
        reject_with_error = true;
      }
    }
    SdpMungingOutcome outcome = reject_with_error
                                    ? SdpMungingOutcome::kRejected
                                    : SdpMungingOutcome::kAccepted;
    RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.SdpMunging.Outcome",
                              static_cast<int>(outcome),
                              static_cast<int>(SdpMungingOutcome::kMaxValue));
    if (reject_with_error) {
      error = RTCError::InvalidModification(
          "SDP is modified in a non-acceptable way");
      TraceSetLocalDescriptionComplete(error);
      observer->OnSetLocalDescriptionComplete(std::move(error));
      last_sdp_munging_type_ = sdp_munging_type;
      ReportInitialSdpMunging(had_local_description, desc->GetType());
      RTC_HISTOGRAM_ENUMERATION_SPARSE(
          "WebRTC.PeerConnection.SdpMunging.SdpOutcome.Rejected",
          sdp_munging_type, SdpMungingType::kMaxValue);
      return;
    }
    if (sdp_munging_type != kNoModification) {
      RTC_HISTOGRAM_ENUMERATION_SPARSE(
          "WebRTC.PeerConnection.SdpMunging.SdpOutcome.Accepted",
          sdp_munging_type, SdpMungingType::kMaxValue);
    }
  }

  // Workaround for isses.webrtc.org/412904801 - detect if packetization:raw
  // was added in SDP munging, and if so, push it back into the codec vendor.
  std::vector<std::pair<Codec, Codec>> codecs_mangled_to_raw;
  // Ignore if other mungings have been applied.
  if (last_created_desc &&
      last_created_desc->description()->contents().size() ==
          desc->description()->contents().size()) {
    for (size_t i = 0; i < desc->description()->contents().size(); ++i) {
      auto last_created_codecs = last_created_desc->description()
                                     ->contents()[i]
                                     .media_description()
                                     ->codecs();
      auto new_codecs =
          desc->description()->contents()[i].media_description()->codecs();
      if (last_created_codecs.size() != new_codecs.size()) {
        continue;  // Ignore added codecs
      }
      for (size_t j = 0; j < new_codecs.size(); ++j) {
        if (new_codecs[j].type == Codec::Type::kVideo &&
            new_codecs[j].name == last_created_codecs[j].name &&
            new_codecs[j].id == last_created_codecs[j].id &&
            !last_created_codecs[j].packetization &&
            new_codecs[j].packetization == kPacketizationParamRaw) {
          codecs_mangled_to_raw.push_back(
              std::pair(last_created_codecs[j], new_codecs[j]));
        }
      }
    }
  }

  // Grab the description type before moving ownership to
  // ApplyLocalDescription, which may destroy it before returning.
  const SdpType type = desc->GetType();

  error = ApplyLocalDescription(std::move(desc), bundle_groups_by_mid);
  // `desc` may be destroyed at this point.

  if (!error.ok()) {
    // If ApplyLocalDescription fails, the PeerConnection could be in an
    // inconsistent state, so act conservatively here and set the session error
    // so that future calls to SetLocalDescription/SetRemoteDescription fail.
    SetSessionError(SessionError::kContent, error.message());
    std::string error_message =
        GetSetDescriptionErrorMessage(CS_LOCAL, type, error);
    RTC_LOG(LS_ERROR) << error_message;
    error = RTCError(RTCErrorType::INTERNAL_ERROR, std::move(error_message));
    TraceSetLocalDescriptionComplete(error);
    observer->OnSetLocalDescriptionComplete(std::move(error));
    return;
  }
  RTC_DCHECK(local_description());

  // Workaround for issues.webrtc.org/412904801 part 2.
  // If application was successful, we change the codec vendor's codec
  // tables according to the mangle.
  // Note that this depends on there being a single codec vendor for all MIDs.
  // We call SetRawPacketization in the redesign path and ModifyVideoCodecs in
  // the legacy path to avoid them interfering with each other.
  if (codec_lookup_helper_->GetCodecVendor()->payload_types_in_transport()) {
    for (const auto& change : codecs_mangled_to_raw) {
      codec_lookup_helper_->GetCodecVendor()->SetRawPacketization(change.first);
    }
  } else {
    codec_lookup_helper_->GetCodecVendor()->ModifyVideoCodecs(
        codecs_mangled_to_raw);
  }

  const bool is_answer = local_description()->GetType() == SdpType::kAnswer;
  if (is_answer) {
    RemoveStoppedTransceivers();
  }

  // Clear last created offer/answer and update SDP munging type.
  last_created_offer_.reset(nullptr);
  last_created_answer_.reset(nullptr);
  last_sdp_munging_type_ = sdp_munging_type;

  // Report SDP munging of the initial call to setLocalDescription separately.
  ReportInitialSdpMunging(had_local_description,
                          local_description()->GetType());

  TraceSetLocalDescriptionComplete(RTCError::OK());
  observer->OnSetLocalDescriptionComplete(RTCError::OK());
  // The observer may have closed the PeerConnection. If so, the transport
  // controller is gone and there is nothing left to do.
  if (pc_->IsClosed()) {
    return;
  }
  pc_->NoteUsageEvent(UsageEvent::SET_LOCAL_DESCRIPTION_SUCCEEDED);

  // Check if negotiation is needed. We must do this after informing the
  // observer that SetLocalDescription() has completed to ensure negotiation
  // is not needed prior to the promise resolving.
  if (IsUnifiedPlan()) {
    bool was_negotiation_needed = is_negotiation_needed_;
    UpdateNegotiationNeeded();
    if (signaling_state() == PeerConnectionInterface::kStable &&
        was_negotiation_needed && is_negotiation_needed_) {
      // Legacy version.
      pc_->RunWithObserver(
          [&](auto observer) { observer->OnRenegotiationNeeded(); });
      // Spec-compliant version; the event may get invalidated before firing.
      GenerateNegotiationNeededEvent();
    }
  }

  // MaybeStartGathering needs to be called after informing the observer so
  // that we don't signal any candidates before signaling that
  // SetLocalDescription completed.
  // For an answer, the candidate pool is discarded as well. This must happen
  // after gathering has started, otherwise the new transports can't take
  // sessions from the pool (see webrtc:572439023).
  if (is_answer) {
    UpdateCachedIceCredentials({});
  }
  transport_controller_s()->MaybeStartGathering(
      /*discard_candidate_pool=*/is_answer,
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(),
       generation = cached_pooled_ice_credentials_generation_](
          std::vector<IceParameters> credentials) {
        if (this_weak_ptr) {
          this_weak_ptr->OnPooledIceCredentials(generation,
                                                std::move(credentials));
        }
      });
}

void SdpOfferAnswerHandler::DoCreateOffer(
    const PeerConnectionInterface::RTCOfferAnswerOptions& options,
    scoped_refptr<CreateSessionDescriptionObserver> observer) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::DoCreateOffer");

  if (!observer) {
    RTC_LOG(LS_ERROR) << "CreateOffer - observer is NULL.";
    return;
  }

  // If a session error has occurred the PeerConnection is in a possibly
  // inconsistent state so fail right away.
  if (session_error() != SessionError::kNone) {
    std::string error_message = GetSessionErrorMsg();
    RTC_LOG(LS_ERROR) << "CreateOffer: " << error_message;
    pc_->message_handler()->PostCreateSessionDescriptionFailure(
        observer.get(),
        RTCError(RTCErrorType::INTERNAL_ERROR, std::move(error_message)));
    return;
  }

  if (!ValidateOfferAnswerOptions(options)) {
    std::string error = "CreateOffer called with invalid options.";
    RTC_LOG(LS_ERROR) << error;
    pc_->message_handler()->PostCreateSessionDescriptionFailure(
        observer.get(),
        RTCError(RTCErrorType::INVALID_PARAMETER, std::move(error)));
    return;
  }

  // Legacy handling for offer_to_receive_audio and offer_to_receive_video.
  // Specified in WebRTC section 4.4.3.2 "Legacy configuration extensions".
  if (IsUnifiedPlan()) {
    RTCError error = HandleLegacyOfferOptions(options);
    if (!error.ok()) {
      pc_->message_handler()->PostCreateSessionDescriptionFailure(
          observer.get(), std::move(error));
      return;
    }
  }

  MediaSessionOptions session_options;
  GetOptionsForOffer(options, &session_options);
  auto observer_wrapper =
      make_ref_counted<CreateDescriptionObserverWrapperWithCreationCallback>(
          [this, state_version = state_version_,
           options](std::unique_ptr<SessionDescriptionInterface> desc) {
            RTC_DCHECK_RUN_ON(signaling_thread());
            last_created_offer_ = std::move(desc);
            last_created_offer_version_ = state_version;
#if RTC_DCHECK_IS_ON
            last_created_offer_options_ = options;
#else
            RTC_UNUSED(options);
#endif
          },
          std::move(observer));
  webrtc_session_desc_factory_->CreateOffer(observer_wrapper.get(), options,
                                            session_options);
}

void SdpOfferAnswerHandler::CreateAnswer(
    CreateSessionDescriptionObserver* observer,
    const PeerConnectionInterface::RTCOfferAnswerOptions& options) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::CreateAnswer");
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (auto* tracer = pc_->tracer()) {
    tracer->OnCreateAnswer(options);
  }
  // Chain this operation. If asynchronous operations are pending on the chain,
  // this operation will be queued to be invoked, otherwise the contents of the
  // lambda will execute immediately.
  operations_chain_->ChainOperation(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(),
       observer_refptr =
           scoped_refptr<CreateSessionDescriptionObserver>(observer),
       options](std::function<void()> operations_chain_callback) {
        // Abort early if `this_weak_ptr` is no longer valid.
        if (!this_weak_ptr) {
          observer_refptr->OnFailure(RTCError(
              RTCErrorType::INTERNAL_ERROR,
              "CreateAnswer failed because the session was shut down"));
          operations_chain_callback();
          return;
        }
        // The operation completes asynchronously when the wrapper is invoked.
        auto observer_wrapper =
            make_ref_counted<CreateSessionDescriptionObserverOperationWrapper>(
                std::move(observer_refptr),
                std::move(operations_chain_callback), this_weak_ptr,
                SdpType::kAnswer);
        if (this_weak_ptr->pc_->IsClosed()) {
          const absl::string_view error =
              "CreateAnswer called when PeerConnection is closed.";
          RTC_LOG(LS_ERROR) << error;
          observer_wrapper->OnFailure(
              RTCError(RTCErrorType::INVALID_STATE, error));
          return;
        }
        const auto signaling_state = this_weak_ptr->signaling_state();
        if (signaling_state != PeerConnectionInterface::kHaveRemoteOffer &&
            signaling_state != PeerConnectionInterface::kHaveLocalPrAnswer) {
          const absl::string_view error =
              "PeerConnection cannot create an answer in a state other than "
              "have-remote-offer or have-local-pranswer.";
          RTC_LOG(LS_ERROR) << error;
          observer_wrapper->OnFailure(
              RTCError(RTCErrorType::INVALID_STATE, error));
          return;
        }
        this_weak_ptr->DoCreateAnswer(options, observer_wrapper);
      });
}

void SdpOfferAnswerHandler::DoCreateAnswer(
    const PeerConnectionInterface::RTCOfferAnswerOptions& options,
    scoped_refptr<CreateSessionDescriptionObserver> observer) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::DoCreateAnswer");
  if (!observer) {
    RTC_LOG(LS_ERROR) << "CreateAnswer - observer is NULL.";
    return;
  }

  // If a session error has occurred the PeerConnection is in a possibly
  // inconsistent state so fail right away.
  if (session_error() != SessionError::kNone) {
    std::string error_message = GetSessionErrorMsg();
    RTC_LOG(LS_ERROR) << "CreateAnswer: " << error_message;
    pc_->message_handler()->PostCreateSessionDescriptionFailure(
        observer.get(),
        RTCError(RTCErrorType::INTERNAL_ERROR, std::move(error_message)));
    return;
  }

  // The remote description should be set if we're in the right state.
  RTC_DCHECK(remote_description());

  if (IsUnifiedPlan()) {
    if (options.offer_to_receive_audio !=
        PeerConnectionInterface::RTCOfferAnswerOptions::kUndefined) {
      RTC_LOG(LS_WARNING) << "CreateAnswer: offer_to_receive_audio is not "
                             "supported with Unified Plan semantics. Use the "
                             "RtpTransceiver API instead.";
    }
    if (options.offer_to_receive_video !=
        PeerConnectionInterface::RTCOfferAnswerOptions::kUndefined) {
      RTC_LOG(LS_WARNING) << "CreateAnswer: offer_to_receive_video is not "
                             "supported with Unified Plan semantics. Use the "
                             "RtpTransceiver API instead.";
    }
  }

  MediaSessionOptions session_options;
  GetOptionsForAnswer(options, &session_options);
  auto observer_wrapper =
      make_ref_counted<CreateDescriptionObserverWrapperWithCreationCallback>(
          [this, state_version = state_version_,
           options](std::unique_ptr<SessionDescriptionInterface> desc) {
            RTC_DCHECK_RUN_ON(signaling_thread());
            last_created_answer_ = std::move(desc);
            last_created_answer_version_ = state_version;
#if RTC_DCHECK_IS_ON
            last_created_answer_options_ = options;
#else
            RTC_UNUSED(options);
#endif
          },
          std::move(observer));
  webrtc_session_desc_factory_->CreateAnswer(observer_wrapper.get(),
                                             session_options);
}

void SdpOfferAnswerHandler::DoSetRemoteDescription(
    std::unique_ptr<RemoteDescriptionOperation> operation) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::DoSetRemoteDescription");

  if (!operation->ok())
    return;

  if (operation->HaveSessionError())
    return;

  if (operation->MaybeRollback())
    return;

  operation->ReportOfferAnswerUma();

  // Handle remote descriptions missing a=mid lines for interop with legacy
  // end points.
  FillInMissingRemoteMids(operation->description());
  if (!operation->IsDescriptionValid())
    return;

  ApplyRemoteDescription(std::move(operation));
}

// Called after a DoSetRemoteDescription operation completes.
void SdpOfferAnswerHandler::SetRemoteDescriptionPostProcess(bool was_answer) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK(remote_description());

  if (was_answer) {
    transport_controller_s()->DiscardCandidatePool();
    // Also drops replies of earlier MaybeStartGathering() calls that are still
    // in flight. Their credentials are from the pool that is being discarded
    // and can include the ones that are in use.
    UpdateCachedIceCredentials({});
  }

  pc_->NoteUsageEvent(UsageEvent::SET_REMOTE_DESCRIPTION_SUCCEEDED);

  // Check if negotiation is needed. We must do this after informing the
  // observer that SetRemoteDescription() has completed to ensure negotiation
  // is not needed prior to the promise resolving.
  if (IsUnifiedPlan()) {
    bool was_negotiation_needed = is_negotiation_needed_;
    UpdateNegotiationNeeded();
    if (signaling_state() == PeerConnectionInterface::kStable &&
        was_negotiation_needed && is_negotiation_needed_) {
      // Legacy version.
      pc_->RunWithObserver(
          [&](auto observer) { observer->OnRenegotiationNeeded(); });
      // Spec-compliant version; the event may get invalidated before firing.
      GenerateNegotiationNeededEvent();
    }
  }
}

void SdpOfferAnswerHandler::SetAssociatedRemoteStreams(
    scoped_refptr<RtpReceiverInternal> receiver,
    const std::vector<std::string>& stream_ids,
    std::vector<scoped_refptr<MediaStreamInterface>>* added_streams,
    std::vector<scoped_refptr<MediaStreamInterface>>* removed_streams) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<scoped_refptr<MediaStreamInterface>> media_streams;
  for (const std::string& stream_id : stream_ids) {
    scoped_refptr<MediaStreamInterface> stream(
        remote_streams_->find(stream_id));
    if (!stream) {
      stream = MediaStreamProxy::Create(Thread::Current(),
                                        MediaStream::Create(stream_id));
      remote_streams_->AddStream(stream);
      added_streams->push_back(stream);
    }
    media_streams.push_back(stream);
  }
  // Special case: "a=msid" missing, use random stream ID.
  if (media_streams.empty() &&
      !(remote_description()->description()->msid_signaling() &
        kMsidSignalingMediaSection)) {
    if (!missing_msid_default_stream_) {
      missing_msid_default_stream_ = MediaStreamProxy::Create(
          Thread::Current(), MediaStream::Create(CreateRandomUuid()));
      added_streams->push_back(missing_msid_default_stream_);
    }
    media_streams.push_back(missing_msid_default_stream_);
  }
  std::vector<scoped_refptr<MediaStreamInterface>> previous_streams =
      receiver->streams();
  // SetStreams() will add/remove the receiver's track to/from the streams.
  // This differs from the spec - the spec uses an "addList" and "removeList"
  // to update the stream-track relationships in a later step. We do this
  // earlier, changing the order of things, but the end-result is the same.
  // TODO(hbos): When we remove remote_streams(), use set_stream_ids()
  // instead. https://crbug.com/webrtc/9480
  receiver->SetStreams(media_streams);
  RemoveRemoteStreamsIfEmpty(previous_streams, removed_streams);
}

bool SdpOfferAnswerHandler::AddIceCandidate(const IceCandidate* ice_candidate) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  const AddIceCandidateResult result = AddIceCandidateInternal(ice_candidate);
  // If the return value is kAddIceCandidateFailNotReady, the candidate has
  // been added, although not 'ready', but that's a success.
  return result == kAddIceCandidateSuccess ||
         result == kAddIceCandidateFailNotReady;
}

AddIceCandidateResult SdpOfferAnswerHandler::AddIceCandidateInternal(
    const IceCandidate* ice_candidate) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::AddIceCandidate");
  if (pc_->IsClosed()) {
    RTC_LOG(LS_ERROR) << "AddIceCandidate: PeerConnection is closed.";
    return kAddIceCandidateFailClosed;
  }

  if (!remote_description()) {
    RTC_LOG(LS_ERROR) << "AddIceCandidate: ICE candidates can't be added "
                         "without any remote session description.";
    return kAddIceCandidateFailNoRemoteDescription;
  }

  if (!ice_candidate) {
    RTC_LOG(LS_ERROR) << "AddIceCandidate: Candidate is null.";
    return kAddIceCandidateFailNullCandidate;
  }

  bool valid = false;
  bool ready = ReadyToUseRemoteCandidate(ice_candidate, nullptr, &valid);
  if (!valid) {
    return kAddIceCandidateFailNotValid;
  }

  // Add this candidate to the remote session description.
  if (!mutable_remote_description()->AddCandidate(ice_candidate)) {
    RTC_LOG(LS_ERROR) << "AddIceCandidate: Candidate cannot be used.";
    return kAddIceCandidateFailInAddition;
  }

  if (!ready) {
    RTC_LOG(LS_INFO) << "AddIceCandidate: Not ready to use candidate.";
    return kAddIceCandidateFailNotReady;
  }

  if (!UseCandidate(ice_candidate)) {
    return kAddIceCandidateFailNotUsable;
  }

  pc_->NoteUsageEvent(UsageEvent::ADD_ICE_CANDIDATE_SUCCEEDED);

  return kAddIceCandidateSuccess;
}

void SdpOfferAnswerHandler::AddIceCandidate(
    std::unique_ptr<IceCandidate> candidate,
    absl::AnyInvocable<void(RTCError, absl::AnyInvocable<void() &&>) &&>
        callback) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::AddIceCandidate");
  RTC_DCHECK_RUN_ON(signaling_thread());
  // Chain this operation. If asynchronous operations are pending on the
  // chain, this operation will be queued to be invoked, otherwise the
  // contents of the lambda will execute immediately.
  operations_chain_->ChainOperation(
      [this_weak_ptr = weak_ptr_factory_.GetWeakPtr(),
       candidate = std::move(candidate), callback = std::move(callback)](
          std::function<void()> operations_chain_callback) mutable {
        auto result =
            this_weak_ptr
                ? this_weak_ptr->AddIceCandidateInternal(candidate.get())
                : kAddIceCandidateFailClosed;
        RTCError error = RTCError::OK();
        switch (result) {
          case AddIceCandidateResult::kAddIceCandidateSuccess:
          case AddIceCandidateResult::kAddIceCandidateFailNotReady:
            break;
          case AddIceCandidateResult::kAddIceCandidateFailClosed:
            // Note that the spec says to just abort without resolving the
            // promise in this case, but this layer must return an RTCError.
            error = RTCError(
                RTCErrorType::INVALID_STATE,
                "AddIceCandidate failed because the session was shut down");
            break;
          case AddIceCandidateResult::kAddIceCandidateFailNoRemoteDescription:
            // Spec: "If remoteDescription is null return a promise rejected
            // with a newly created InvalidStateError."
            error = RTCError(RTCErrorType::INVALID_STATE,
                             "The remote description was null");
            break;
          case AddIceCandidateResult::kAddIceCandidateFailNullCandidate:
            // TODO(https://crbug.com/935898): Handle end-of-candidates instead
            // of treating null candidate as an error.
            error = RTCError(RTCErrorType::UNSUPPORTED_OPERATION,
                             "Error processing ICE candidate");
            break;
          case AddIceCandidateResult::kAddIceCandidateFailNotValid:
          case AddIceCandidateResult::kAddIceCandidateFailInAddition:
          case AddIceCandidateResult::kAddIceCandidateFailNotUsable:
            // Spec: "If candidate could not be successfully added [...] Reject
            // p with a newly created OperationError and abort these steps."
            // UNSUPPORTED_OPERATION maps to OperationError.
            error = RTCError(RTCErrorType::UNSUPPORTED_OPERATION,
                             "Error processing ICE candidate");
            break;
          default:
            RTC_DCHECK_NOTREACHED();
            error = RTCError(RTCErrorType::INTERNAL_ERROR,
                             "Unexpected AddIceCandidate result");
            break;
        }
        std::move(callback)(std::move(error),
                            std::move(operations_chain_callback));
      });
}

bool SdpOfferAnswerHandler::RemoveIceCandidate(const IceCandidate* candidate) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::RemoveIceCandidate");
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (pc_->IsClosed() || !remote_description() || !candidate) {
    RTC_LOG(LS_ERROR) << "RemoveIceCandidate: PeerConnection is closed.";
    return false;
  }

  bool removed = mutable_remote_description()->RemoveCandidate(candidate);
  // Remove the candidate from the transport controller. This doesn't block;
  // the candidate is removed on the network thread.
  transport_controller_s()->RemoveRemoteCandidate(candidate);
  return removed;
}

void SdpOfferAnswerHandler::AddLocalIceCandidate(
    const IceCandidate* candidate) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (local_description()) {
    mutable_local_description()->AddCandidate(candidate);
  }
}

void SdpOfferAnswerHandler::RemoveLocalIceCandidates(
    absl::string_view mid,
    const std::vector<Candidate>& candidates) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (local_description()) {
    for (const auto& c : candidates) {
      IceCandidate ice_candidate(mid, -1, c);
      mutable_local_description()->RemoveCandidate(&ice_candidate);
    }
  }
}

const SessionDescriptionInterface* SdpOfferAnswerHandler::local_description()
    const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return pending_local_description_ ? pending_local_description_.get()
                                    : current_local_description_.get();
}

const SessionDescriptionInterface* SdpOfferAnswerHandler::remote_description()
    const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return pending_remote_description_ ? pending_remote_description_.get()
                                     : current_remote_description_.get();
}

const SessionDescriptionInterface*
SdpOfferAnswerHandler::current_local_description() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return current_local_description_.get();
}

const SessionDescriptionInterface*
SdpOfferAnswerHandler::current_remote_description() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return current_remote_description_.get();
}

const SessionDescriptionInterface*
SdpOfferAnswerHandler::pending_local_description() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return pending_local_description_.get();
}

const SessionDescriptionInterface*
SdpOfferAnswerHandler::pending_remote_description() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return pending_remote_description_.get();
}

PeerConnectionInterface::SignalingState SdpOfferAnswerHandler::signaling_state()
    const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return signaling_state_;
}

void SdpOfferAnswerHandler::ChangeSignalingState(
    PeerConnectionInterface::SignalingState signaling_state) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::ChangeSignalingState");
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (signaling_state_ == signaling_state) {
    return;
  }
  RTC_LOG(LS_INFO) << "Session: " << pc_->session_id() << " Old state: "
                   << PeerConnectionInterface::AsString(signaling_state_)
                   << " New state: "
                   << PeerConnectionInterface::AsString(signaling_state);
  signaling_state_ = signaling_state;
  if (auto* tracer = pc_->tracer()) {
    tracer->OnSignalingStateChanged(signaling_state);
  }
  pc_->RunWithObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    observer->OnSignalingChange(signaling_state_);
  });
}

RTCError SdpOfferAnswerHandler::UpdateSessionState(
    SdpType type,
    ContentSource source,
    const SessionDescription* description,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  // If there's already a pending error then no state transition should
  // happen. But all call-sites should be verifying this before calling us!
  RTC_DCHECK(session_error() == SessionError::kNone);

  // If this is answer-ish we're ready to let media flow.
  if (type == SdpType::kPrAnswer || type == SdpType::kAnswer) {
    EnableSending();
  }

  // Update the signaling state according to the specified state machine (see
  // https://w3c.github.io/webrtc-pc/#rtcsignalingstate-enum).
  if (type == SdpType::kOffer) {
    ChangeSignalingState(source == CS_LOCAL
                             ? PeerConnectionInterface::kHaveLocalOffer
                             : PeerConnectionInterface::kHaveRemoteOffer);
  } else if (type == SdpType::kPrAnswer) {
    ChangeSignalingState(source == CS_LOCAL
                             ? PeerConnectionInterface::kHaveLocalPrAnswer
                             : PeerConnectionInterface::kHaveRemotePrAnswer);
  } else {
    RTC_DCHECK(type == SdpType::kAnswer);
    ChangeSignalingState(PeerConnectionInterface::kStable);
    if (ConfiguredForMedia()) {
      transceivers()->DiscardStableStates();
    }
  }

  // Update internal objects according to the session description's media
  // descriptions.
  return PushdownMediaDescription(type, source, bundle_groups_by_mid);
}

bool SdpOfferAnswerHandler::ShouldFireNegotiationNeededEvent(
    uint32_t event_id) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // Plan B? Always fire to conform with useless legacy behavior.
  if (!IsUnifiedPlan()) {
    return true;
  }
  // The event ID has been invalidated. Either negotiation is no longer needed
  // or a newer negotiation needed event has been generated.
  if (event_id != negotiation_needed_event_id_) {
    return false;
  }
  // The chain is no longer empty, update negotiation needed when it becomes
  // empty. This should generate a newer negotiation needed event, making this
  // one obsolete.
  if (!operations_chain_->IsEmpty()) {
    // Since we just suppressed an event that would have been fired, if
    // negotiation is still needed by the time the chain becomes empty again,
    // we must make sure to generate another event if negotiation is needed
    // then. This happens when `is_negotiation_needed_` goes from false to
    // true, so we set it to false until UpdateNegotiationNeeded() is called.
    is_negotiation_needed_ = false;
    update_negotiation_needed_on_empty_chain_ = true;
    return false;
  }
  // We must not fire if the signaling state is no longer "stable". If
  // negotiation is still needed when we return to "stable", a new negotiation
  // needed event will be generated, so this one can safely be suppressed.
  if (signaling_state_ != PeerConnectionInterface::kStable) {
    return false;
  }
  // All checks have passed - please fire "negotiationneeded" now!
  return true;
}

scoped_refptr<StreamCollectionInterface>
SdpOfferAnswerHandler::local_streams() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(!IsUnifiedPlan()) << "local_streams is not available with Unified "
                                 "Plan SdpSemantics. Please use GetSenders "
                                 "instead.";
  return local_streams_;
}

scoped_refptr<StreamCollectionInterface>
SdpOfferAnswerHandler::remote_streams() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(!IsUnifiedPlan()) << "remote_streams is not available with Unified "
                                 "Plan SdpSemantics. Please use GetReceivers "
                                 "instead.";
  return remote_streams_;
}

bool SdpOfferAnswerHandler::AddStream(MediaStreamInterface* local_stream) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(!IsUnifiedPlan()) << "AddStream is not available with Unified Plan "
                                 "SdpSemantics. Please use AddTrack instead.";
  if (pc_->IsClosed()) {
    return false;
  }
  if (!CanAddLocalMediaStream(local_streams_.get(), local_stream)) {
    return false;
  }

  local_streams_->AddStream(scoped_refptr<MediaStreamInterface>(local_stream));
  auto observer = std::make_unique<MediaStreamObserver>(
      local_stream,
      [this](AudioTrackInterface* audio_track,
             MediaStreamInterface* media_stream) {
        RTC_DCHECK_RUN_ON(signaling_thread());
        OnAudioTrackAdded(audio_track, media_stream);
      },
      [this](AudioTrackInterface* audio_track,
             MediaStreamInterface* media_stream) {
        RTC_DCHECK_RUN_ON(signaling_thread());
        OnAudioTrackRemoved(audio_track, media_stream);
      },
      [this](VideoTrackInterface* video_track,
             MediaStreamInterface* media_stream) {
        RTC_DCHECK_RUN_ON(signaling_thread());
        OnVideoTrackAdded(video_track, media_stream);
      },
      [this](VideoTrackInterface* video_track,
             MediaStreamInterface* media_stream) {
        RTC_DCHECK_RUN_ON(signaling_thread());
        OnVideoTrackRemoved(video_track, media_stream);
      });
  stream_observers_.push_back(std::move(observer));

  for (const auto& track : local_stream->GetAudioTracks()) {
    rtp_manager()->AddTrackPlanB(track.get(), local_stream);
  }
  for (const auto& track : local_stream->GetVideoTracks()) {
    rtp_manager()->AddTrackPlanB(track.get(), local_stream);
  }

  pc_->legacy_stats()->AddStream(local_stream);
  UpdateNegotiationNeeded();
  return true;
}

void SdpOfferAnswerHandler::RemoveStream(MediaStreamInterface* local_stream) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(!IsUnifiedPlan()) << "RemoveStream is not available with Unified "
                                 "Plan SdpSemantics. Please use RemoveTrack "
                                 "instead.";
  TRACE_EVENT0("webrtc", "PeerConnection::RemoveStream");
  if (!pc_->IsClosed()) {
    for (const auto& track : local_stream->GetAudioTracks()) {
      rtp_manager()->RemoveTrackPlanB(track.get(), local_stream);
    }
    for (const auto& track : local_stream->GetVideoTracks()) {
      rtp_manager()->RemoveTrackPlanB(track.get(), local_stream);
    }
  }
  local_streams_->RemoveStream(local_stream);
  std::erase_if(
      stream_observers_,
      [local_stream](const std::unique_ptr<MediaStreamObserver>& observer) {
        return observer->stream()->id().compare(local_stream->id()) == 0;
      });

  if (pc_->IsClosed()) {
    return;
  }
  UpdateNegotiationNeeded();
}

void SdpOfferAnswerHandler::OnAudioTrackAdded(AudioTrackInterface* track,
                                              MediaStreamInterface* stream) {
  RTC_DCHECK(!IsUnifiedPlan());
  if (pc_->IsClosed()) {
    return;
  }
  rtp_manager()->AddTrackPlanB(track, stream);
  UpdateNegotiationNeeded();
}

void SdpOfferAnswerHandler::OnAudioTrackRemoved(AudioTrackInterface* track,
                                                MediaStreamInterface* stream) {
  RTC_DCHECK(!IsUnifiedPlan());
  if (pc_->IsClosed()) {
    return;
  }
  rtp_manager()->RemoveTrackPlanB(track, stream);
  UpdateNegotiationNeeded();
}

void SdpOfferAnswerHandler::OnVideoTrackAdded(VideoTrackInterface* track,
                                              MediaStreamInterface* stream) {
  RTC_DCHECK(!IsUnifiedPlan());
  if (pc_->IsClosed()) {
    return;
  }
  rtp_manager()->AddTrackPlanB(track, stream);
  UpdateNegotiationNeeded();
}

void SdpOfferAnswerHandler::OnVideoTrackRemoved(VideoTrackInterface* track,
                                                MediaStreamInterface* stream) {
  RTC_DCHECK(!IsUnifiedPlan());
  if (pc_->IsClosed()) {
    return;
  }
  rtp_manager()->RemoveTrackPlanB(track, stream);
  UpdateNegotiationNeeded();
}

RTCError SdpOfferAnswerHandler::Rollback(SdpType desc_type) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::Rollback");
  auto state = signaling_state();
  if (state != PeerConnectionInterface::kHaveLocalOffer &&
      state != PeerConnectionInterface::kHaveRemoteOffer) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INVALID_STATE)
        << "Called in wrong signalingState: "
        << PeerConnectionInterface::AsString(signaling_state()));
  }
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(IsUnifiedPlan());
  std::vector<scoped_refptr<RtpTransceiverInterface>>
      now_receiving_transceivers;
  std::vector<scoped_refptr<MediaStreamInterface>> all_added_streams;
  std::vector<scoped_refptr<MediaStreamInterface>> all_removed_streams;
  std::vector<scoped_refptr<RtpReceiverInterface>> removed_receivers;
  // Senders whose pre-offer encodings are restored after the channel teardown
  // tasks below have run.
  std::vector<std::pair<scoped_refptr<RtpSenderInternal>,
                        std::vector<RtpEncodingParameters>>>
      senders_to_restore;

  // Keep to-be-removed transceivers alive until after tasks for them have been
  // run.
  std::vector<RtpTransceiverProxyRefPtr> transceivers_to_remove;
  absl::Cleanup cleanup_remove = [&] {
    for (const auto& transceiver : transceivers_to_remove) {
      transceivers()->Remove(transceiver);
    }
  };
  ScopedOperationsBatcher worker_tasks(context_->worker_thread());
  ScopedOperationsBatcher network_tasks(context_->network_thread());

  for (auto&& transceivers_stable_state_pair : transceivers()->StableStates()) {
    auto transceiver = transceivers_stable_state_pair.first;
    auto stable_state = transceivers_stable_state_pair.second;

    if (stable_state.did_set_fired_direction()) {
      // If this rollback triggers going from not receiving to receiving again,
      // we need to fire "ontrack".
      bool previously_fired_direction_is_recv =
          transceiver->fired_direction().has_value() &&
          RtpTransceiverDirectionHasRecv(*transceiver->fired_direction());
      bool currently_fired_direction_is_recv =
          stable_state.fired_direction().has_value() &&
          RtpTransceiverDirectionHasRecv(
              stable_state.fired_direction().value());
      if (!previously_fired_direction_is_recv &&
          currently_fired_direction_is_recv) {
        now_receiving_transceivers.push_back(transceiver);
      }

      transceiver->internal()->set_fired_direction(
          stable_state.fired_direction());
    }

    // https://github.com/w3c/webrtc-pc/issues/3081
    transceiver->internal()->set_receptive(
        transceiver->internal()->current_direction() &&
        RtpTransceiverDirectionHasRecv(
            *transceiver->internal()->current_direction()));

    if (stable_state.remote_stream_ids()) {
      std::vector<scoped_refptr<MediaStreamInterface>> added_streams;
      std::vector<scoped_refptr<MediaStreamInterface>> removed_streams;
      SetAssociatedRemoteStreams(transceiver->internal()->receiver_internal(),
                                 stable_state.remote_stream_ids().value(),
                                 &added_streams, &removed_streams);
      all_added_streams.insert(all_added_streams.end(), added_streams.begin(),
                               added_streams.end());
      all_removed_streams.insert(all_removed_streams.end(),
                                 removed_streams.begin(),
                                 removed_streams.end());
      if (!stable_state.has_m_section() && !stable_state.newly_created()) {
        continue;
      }
    }

    // Due to the above `continue` statement, the below code only runs if there
    // is a change in mid association (has_m_section), if the transceiver was
    // newly created (newly_created) or if remote streams were not set.

    RTC_DCHECK(transceiver->internal()->mid().has_value());
    network_tasks.Add(transceiver->internal()->GetClearChannelNetworkTask());
    auto sender_internal = transceiver->internal()->sender_internal();
    // The channel the sender was configured against is about to go away.
    // Detach the sender from its send stream first, so that it returns to the
    // un-negotiated state instead of holding on to an ssrc that no longer
    // exists.
    worker_tasks.AddWithFinalizer(
        sender_internal->SetSsrcTask(0, /*layer_count=*/0));
    worker_tasks.Add(transceiver->internal()->GetDeleteChannelWorkerTask(
        /*stop_senders=*/false));

    if (signaling_state() == PeerConnectionInterface::kHaveRemoteOffer &&
        transceiver->receiver()) {
      removed_receivers.push_back(transceiver->receiver());
    }
    if (stable_state.newly_created()) {
      if (transceiver->internal()->reused_for_addtrack()) {
        transceiver->internal()->set_created_by_addtrack(true);
      } else {
        worker_tasks.Add(
            transceiver->internal()->GetStopTransceiverProcedure());
        transceivers_to_remove.push_back(transceiver);
      }
    }
    if (stable_state.init_send_encodings()) {
      // Restore the encodings once the tasks above have run, since detaching
      // the sender from its send stream clears them.
      senders_to_restore.emplace_back(std::move(sender_internal),
                                      *stable_state.init_send_encodings());
    }

    transceiver->internal()->SetTransport(nullptr, std::nullopt);
    if (stable_state.has_m_section()) {
      transceiver->internal()->set_mid(stable_state.mid());
      transceiver->internal()->set_mline_index(stable_state.mline_index());
    }
  }

  // Roll back the transports in the same batch, after the tasks above have
  // detached the channels from their transports. Rolling back destroys the
  // transports that are no longer used, which can include transports that
  // those channels are attached to. GetDeleteChannelWorkerTask() has already
  // made the channels unreachable from their transceivers, so the rollback
  // can't attach them to a transport again.
  network_tasks.AddWithFinalizer(transport_controller_s()->MakeRollbackTask());

  RTCError rollback_error = network_tasks.Run();
  RTCError e = worker_tasks.Run();
  RTC_DCHECK(e.ok());  // only void tasks queued.

  for (const auto& [sender, encodings] : senders_to_restore) {
    sender->set_init_send_encodings(encodings);
  }
  if (!rollback_error.ok()) {
    return rollback_error;
  }
  transceivers()->DiscardStableStates();
  pending_local_description_.reset();
  pending_remote_description_.reset();
  ChangeSignalingState(PeerConnectionInterface::kStable);

  if (auto* tracer = pc_->tracer()) {
    for (const auto& transceiver : now_receiving_transceivers) {
      tracer->OnTrack(*transceiver);
    }
  }

  // Once all processing has finished, fire off callbacks.
  pc_->RunWithObserver([&](auto observer) {
    for (const auto& transceiver : now_receiving_transceivers) {
      observer->OnTrack(transceiver);
      observer->OnAddTrack(transceiver->receiver(),
                           transceiver->receiver()->streams());
    }
    for (const auto& receiver : removed_receivers) {
      observer->OnRemoveTrack(receiver);
    }
    for (const auto& stream : all_added_streams) {
      observer->OnAddStream(stream);
    }
    for (const auto& stream : all_removed_streams) {
      observer->OnRemoveStream(stream);
    }
  });

  // The assumption is that in case of implicit rollback
  // UpdateNegotiationNeeded gets called in SetRemoteDescription.
  if (desc_type == SdpType::kRollback) {
    UpdateNegotiationNeeded();
    if (is_negotiation_needed_) {
      // Legacy version.
      pc_->RunWithObserver(
          [&](auto observer) { observer->OnRenegotiationNeeded(); });
      // Spec-compliant version; the event may get invalidated before firing.
      GenerateNegotiationNeededEvent();
    }
  }
  return RTCError::OK();
}

bool SdpOfferAnswerHandler::IsUnifiedPlan() const {
  return pc_->IsUnifiedPlan();
}

void SdpOfferAnswerHandler::OnOperationsChainEmpty() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (pc_->IsClosed() || !update_negotiation_needed_on_empty_chain_)
    return;
  update_negotiation_needed_on_empty_chain_ = false;
  // Firing when chain is empty is only supported in Unified Plan to avoid
  // Plan B regressions. (In Plan B, onnegotiationneeded is already broken
  // anyway, so firing it even more might just be confusing.)
  if (IsUnifiedPlan()) {
    UpdateNegotiationNeeded();
  }
}

std::optional<bool> SdpOfferAnswerHandler::is_caller() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return is_caller_;
}

bool SdpOfferAnswerHandler::HasNewIceCredentials() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return local_ice_credentials_to_replace_->HasIceCredentials();
}

bool SdpOfferAnswerHandler::IceRestartPending(
    const std::string& content_name) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return pending_ice_restarts_.find(content_name) !=
         pending_ice_restarts_.end();
}

bool SdpOfferAnswerHandler::NeedsIceRestart(
    const std::string& content_name) const {
  return pc_->NeedsIceRestart(content_name);
}

std::optional<SSLRole> SdpOfferAnswerHandler::GetDtlsRole(
    const std::string& mid) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return transport_controller_s()->GetDtlsRole(mid);
}

#if RTC_DCHECK_IS_ON
void SdpOfferAnswerHandler::VerifyCachedOffer(
    absl::AnyInvocable<void() &&> on_verified) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(last_created_offer_);
  auto verify_observer =
      make_ref_counted<CreateSessionDescriptionObserverVerify>(
          last_created_offer_->Clone(), std::move(on_verified));
  MediaSessionOptions session_options;
  GetOptionsForOffer(last_created_offer_options_, &session_options);
  webrtc_session_desc_factory_->CreateOffer(
      verify_observer.get(), last_created_offer_options_, session_options);
}

void SdpOfferAnswerHandler::VerifyCachedAnswer(
    absl::AnyInvocable<void() &&> on_verified) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(last_created_answer_);
  auto verify_observer =
      make_ref_counted<CreateSessionDescriptionObserverVerify>(
          last_created_answer_->Clone(), std::move(on_verified));
  MediaSessionOptions session_options;
  GetOptionsForAnswer(last_created_answer_options_, &session_options);
  webrtc_session_desc_factory_->CreateAnswer(verify_observer.get(),
                                             session_options);
}
#endif

void SdpOfferAnswerHandler::UpdateNegotiationNeeded() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  ++state_version_;
  if (!IsUnifiedPlan()) {
    pc_->RunWithObserver(
        [&](auto observer) { observer->OnRenegotiationNeeded(); });
    GenerateNegotiationNeededEvent();
    return;
  }

  // In the spec, a task is queued here to run the following steps - this is
  // meant to ensure we do not fire onnegotiationneeded prematurely if
  // multiple changes are being made at once. In order to support Chromium's
  // implementation where the JavaScript representation of the PeerConnection
  // lives on a separate thread though, the queuing of a task is instead
  // performed by the PeerConnectionObserver posting from the signaling thread
  // to the JavaScript main thread that negotiation is needed. And because the
  // Operations Chain lives on the WebRTC signaling thread,
  // ShouldFireNegotiationNeededEvent() must be called before firing the event
  // to ensure the Operations Chain is still empty and the event has not been
  // invalidated.

  // If connection's [[IsClosed]] slot is true, abort these steps.
  if (pc_->IsClosed())
    return;

  // If connection's signaling state is not "stable", abort these steps.
  if (signaling_state() != PeerConnectionInterface::kStable)
    return;

  // NOTE
  // The negotiation-needed flag will be updated once the state transitions to
  // "stable", as part of the steps for setting an RTCSessionDescription.

  // If the result of checking if negotiation is needed is false, clear the
  // negotiation-needed flag by setting connection's [[NegotiationNeeded]]
  // slot to false, and abort these steps.
  bool is_negotiation_needed = CheckIfNegotiationIsNeeded();
  if (!is_negotiation_needed) {
    is_negotiation_needed_ = false;
    // Invalidate any negotiation needed event that may previosuly have been
    // generated.
    ++negotiation_needed_event_id_;
    return;
  }

  // If connection's [[NegotiationNeeded]] slot is already true, abort these
  // steps.
  if (is_negotiation_needed_)
    return;

  // Set connection's [[NegotiationNeeded]] slot to true.
  is_negotiation_needed_ = true;

  // Queue a task that runs the following steps:
  // If connection's [[IsClosed]] slot is true, abort these steps.
  // If connection's [[NegotiationNeeded]] slot is false, abort these steps.
  // Fire an event named negotiationneeded at connection.
  pc_->RunWithObserver(
      [&](auto observer) { observer->OnRenegotiationNeeded(); });
  // Fire the spec-compliant version; when ShouldFireNegotiationNeededEvent()
  // is used in the task queued by the observer, this event will only fire
  // when the chain is empty.
  GenerateNegotiationNeededEvent();
}

std::optional<SSLRole> SdpOfferAnswerHandler::GuessSslRole() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!pc_->sctp_mid())
    return std::nullopt;

  // TODO(bugs.webrtc.org/13668): This guesswork is guessing wrong (returning
  // SSL_CLIENT = ACTIVE) if remote offer has role ACTIVE, but we'll be able
  // to detect that by looking at the SDP.
  //
  // The phases of establishing an SCTP session are:
  //
  // Offerer:
  //
  // * Before negotiation: Neither is_caller nor sctp_mid exists.
  // * After setting an offer as local description: is_caller is known (true),
  //   sctp_mid is known, but we don't know the SSL role for sure (or if we'll
  //   eventually get an SCTP session).
  // * After setting an answer as the remote description: We know is_caller,
  //   sctp_mid and that we'll get the SCTP channel established (m-section
  //   wasn't rejected).
  // * Special case: The SCTP  m-section was rejected: Close datachannels.
  // * We MAY know the SSL role if we offered actpass and got back active or
  //   passive; if the other end is a webrtc implementation, it will be active.
  // * After the TLS handshake: We have a definitive answer on the SSL role.
  //
  // Answerer:
  //
  // * After setting an offer as remote description: We know is_caller (false).
  // * If there was an SCTP session, we know the SCTP mid. We also know the
  //   SSL role, since if the remote offer was actpass or passive, we'll answer
  //   active, and if the remote offer was active, we're passive.
  // * Special case: No SCTP m= line. We don't know for sure if the remote
  //   doesn't support it or just didn't offer it. Not sure what we do in this
  //   case (logic would suggest fire a `negotiationneeded` event and generate a
  //   subsequent offer, but this needs to be tested).
  // * After the TLS handshake: We know that TLS obeyed the protocol. There
  //   should be an error surfaced somewhere if it didn't.
  // * "Guessing" should always be correct if we get an SCTP session and are not
  //   the offerer.

  return is_caller() ? SSL_SERVER : SSL_CLIENT;
}

bool SdpOfferAnswerHandler::CheckIfNegotiationIsNeeded() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // 1. If any implementation-specific negotiation is required, as described
  // at the start of this section, return true.

  // 2. If connection.[[LocalIceCredentialsToReplace]] is not empty, return
  // true.
  if (local_ice_credentials_to_replace_->HasIceCredentials()) {
    return true;
  }

  // 3. Let description be connection.[[CurrentLocalDescription]].
  const SessionDescriptionInterface* description = current_local_description();
  if (!description)
    return true;

  // 4. If connection has created any RTCDataChannels, and no m= section in
  // description has been negotiated yet for data, return true.
  if (data_channel_controller()->HasUsedDataChannels()) {
    const ContentInfo* data_content =
        GetFirstDataContent(description->description()->contents());
    if (!data_content) {
      return true;
    }
    // The remote end might have rejected the data content.
    const ContentInfo* remote_data_content =
        current_remote_description()
            ? current_remote_description()->description()->GetContentByName(
                  data_content->mid())
            : nullptr;
    if (remote_data_content && remote_data_content->rejected) {
      return true;
    }
  }
  if (!ConfiguredForMedia()) {
    return false;
  }

  // 5. For each transceiver in connection's set of transceivers, perform the
  // following checks:
  for (const auto& transceiver : transceivers()->ListInternal()) {
    const ContentInfo* current_local_msection =
        FindTransceiverMSection(transceiver, description);

    const ContentInfo* current_remote_msection =
        FindTransceiverMSection(transceiver, current_remote_description());

    // 5.4 If transceiver is stopped and is associated with an m= section,
    // but the associated m= section is not yet rejected in
    // connection.[[CurrentLocalDescription]] or
    // connection.[[CurrentRemoteDescription]], return true.
    if (transceiver->stopped()) {
      RTC_DCHECK(transceiver->stopping());
      if (current_local_msection && !current_local_msection->rejected &&
          ((current_remote_msection && !current_remote_msection->rejected) ||
           !current_remote_msection)) {
        return true;
      }
      continue;
    }

    // 5.1 If transceiver.[[Stopping]] is true and transceiver.[[Stopped]] is
    // false, return true.
    if (transceiver->stopping() && !transceiver->stopped())
      return true;

    // 5.2 If transceiver isn't stopped and isn't yet associated with an m=
    // section in description, return true.
    if (!current_local_msection)
      return true;

    const MediaContentDescription* current_local_media_description =
        current_local_msection->media_description();
    // 5.3 If transceiver isn't stopped and is associated with an m= section
    // in description then perform the following checks:

    // If the transceiver's Sframe state differs from the negotiated state
    // in the current local description, negotiation is needed.
    if (transceiver->SframeEnabled().has_value() &&
        transceiver->SframeEnabled().value() !=
            current_local_media_description->sframe_enabled()) {
      return true;
    }

    // 5.3.1 If transceiver.[[Direction]] is "sendrecv" or "sendonly", and the
    // associated m= section in description either doesn't contain a single
    // "a=msid" line, or the number of MSIDs from the "a=msid" lines in this
    // m= section, or the MSID values themselves, differ from what is in
    // transceiver.sender.[[AssociatedMediaStreamIds]], return true.
    if (RtpTransceiverDirectionHasSend(transceiver->direction())) {
      if (current_local_media_description->streams().empty())
        return true;

      std::vector<std::string> msection_msids;
      for (const auto& stream : current_local_media_description->streams()) {
        for (const std::string& msid : stream.stream_ids())
          msection_msids.push_back(msid);
      }

      std::vector<std::string> transceiver_msids =
          transceiver->sender()->stream_ids();
      if (msection_msids.size() != transceiver_msids.size())
        return true;

      absl::c_sort(transceiver_msids);
      absl::c_sort(msection_msids);
      if (transceiver_msids != msection_msids)
        return true;
    }

    // 5.3.2 If description is of type "offer", and the direction of the
    // associated m= section in neither connection.[[CurrentLocalDescription]]
    // nor connection.[[CurrentRemoteDescription]] matches
    // transceiver.[[Direction]], return true.
    if (description->GetType() == SdpType::kOffer) {
      if (!current_remote_description())
        return true;

      if (!current_remote_msection)
        return true;

      RtpTransceiverDirection current_local_direction =
          current_local_media_description->direction();
      RtpTransceiverDirection current_remote_direction =
          current_remote_msection->media_description()->direction();
      if (transceiver->direction() != current_local_direction &&
          transceiver->direction() !=
              RtpTransceiverDirectionReversed(current_remote_direction)) {
        return true;
      }
    }

    // 5.3.3 If description is of type "answer", and the direction of the
    // associated m= section in the description does not match
    // transceiver.[[Direction]] intersected with the offered direction (as
    // described in [JSEP] (section 5.3.1.)), return true.
    if (description->GetType() == SdpType::kAnswer) {
      if (!remote_description())
        return true;

      const ContentInfo* offered_remote_msection =
          FindTransceiverMSection(transceiver, remote_description());

      RtpTransceiverDirection offered_direction =
          offered_remote_msection
              ? offered_remote_msection->media_description()->direction()
              : RtpTransceiverDirection::kInactive;

      if (current_local_media_description->direction() !=
          (RtpTransceiverDirectionIntersection(
              transceiver->direction(),
              RtpTransceiverDirectionReversed(offered_direction)))) {
        return true;
      }
    }
  }
  // If all the preceding checks were performed and true was not returned,
  // nothing remains to be negotiated; return false.
  return false;
}

void SdpOfferAnswerHandler::GenerateNegotiationNeededEvent() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  ++negotiation_needed_event_id_;
  pc_->RunWithObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    observer->OnNegotiationNeededEvent(negotiation_needed_event_id_);
  });
}

RTCError SdpOfferAnswerHandler::ValidateSessionDescription(
    const SessionDescriptionInterface* sdesc,
    ContentSource source,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  // An assumption is that a check for session error is done at a higher level.
  RTC_DCHECK_EQ(SessionError::kNone, session_error());

  if (!sdesc || !sdesc->description()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << kInvalidSdp);
  }

  SdpType type = sdesc->GetType();
  if ((source == CS_LOCAL && !ExpectSetLocalDescription(type)) ||
      (source == CS_REMOTE && !ExpectSetRemoteDescription(type))) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INVALID_STATE)
        << "Called in wrong state: "
        << PeerConnectionInterface::AsString(signaling_state()));
  }

  RTCError error = ValidateMids(*sdesc->description());
  if (!error.ok()) {
    return error;
  }

  // Verify crypto settings.
  if (pc_->dtls_enabled()) {
    RTCError crypto_error = VerifyCrypto(
        sdesc->description(), pc_->dtls_enabled(), bundle_groups_by_mid);
    if (!crypto_error.ok()) {
      return crypto_error;
    }
  }

  error = ValidateCryptex(sdesc->description(), bundle_groups_by_mid,
                          pc_->GetCryptoOptions().srtp.cryptex_policy ==
                              CryptoOptions::Srtp::CryptexPolicy::kRequire);
  if (!error.ok()) {
    return error;
  }

  // Verify ice-ufrag and ice-pwd.
  if (!VerifyIceUfragPwdPresent(sdesc->description(), bundle_groups_by_mid)) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << kSdpWithoutIceUfragPwd);
  }

  // Validate that there are no collisions of bundled payload types.
  error = ValidateBundledPayloadTypes(*sdesc->description());
  RTC_HISTOGRAM_BOOLEAN("WebRTC.PeerConnection.ValidBundledPayloadTypes",
                        error.ok());
  if (!error.ok()) {
    RTC_LOG(LS_ERROR) << "Bundled payload type collision: " << error.message();
    if (env_.field_trials().IsEnabled(
            "WebRTC-SdpBundlePayloadTypeCollisionCheck")) {
      return error;
    }
  }

  // Validate that there are no collisions of bundled header extensions ids.
  error = ValidateBundledRtpHeaderExtensions(*sdesc->description());
  if (!error.ok()) {
    return error;
  }

  // Validate the SSRC groups.
  error = ValidateSsrcGroups(*sdesc->description());
  if (!error.ok()) {
    return error;
  }

  if (!pc_->ValidateBundleSettings(sdesc->description(),
                                   bundle_groups_by_mid)) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << kBundleWithoutRtcpMux);
  }

  error = ValidatePayloadTypes(*sdesc->description());
  if (!error.ok()) {
    return error;
  }

  // TODO(skvlad): When the local rtcp-mux policy is Require, reject any
  // m-lines that do not rtcp-mux enabled.

  // Verify m-lines in Answer when compared against Offer.
  if (type == SdpType::kPrAnswer || type == SdpType::kAnswer) {
    // With an answer we want to compare the new answer session description
    // with the offer's session description from the current negotiation.
    const SessionDescription* offer_desc =
        (source == CS_LOCAL) ? remote_description()->description()
                             : local_description()->description();
    if (!MediaSectionsHaveSameCount(*offer_desc, *sdesc->description()) ||
        !MediaSectionsInSameOrder(*offer_desc, nullptr, *sdesc->description(),
                                  type)) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << kMlineMismatchInAnswer);
    }
  } else {
    // The re-offers should respect the order of m= sections in current
    // description. See RFC3264 Section 8 paragraph 4 for more details.
    // With a re-offer, either the current local or current remote
    // descriptions could be the most up to date, so we would like to check
    // against both of them if they exist. It could be the case that one of
    // them has a 0 port for a media section, but the other does not. This is
    // important to check against in the case that we are recycling an m=
    // section.
    const SessionDescription* current_desc = nullptr;
    const SessionDescription* secondary_current_desc = nullptr;
    if (local_description()) {
      current_desc = local_description()->description();
      if (remote_description()) {
        secondary_current_desc = remote_description()->description();
      }
    } else if (remote_description()) {
      current_desc = remote_description()->description();
    }
    if (current_desc &&
        !MediaSectionsInSameOrder(*current_desc, secondary_current_desc,
                                  *sdesc->description(), type)) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << kMlineMismatchInSubsequentOffer);
    }
  }

  if (IsUnifiedPlan()) {
    // Ensure that each audio and video media section has at most one
    // "StreamParams". This will return an error if receiving a session
    // description from a "Plan B" endpoint which adds multiple tracks of the
    // same type. With Unified Plan, there can only be at most one track per
    // media section.
    for (const ContentInfo& content : sdesc->description()->contents()) {
      const MediaContentDescription& desc = *content.media_description();
      if ((desc.type() == MediaType::AUDIO ||
           desc.type() == MediaType::VIDEO) &&
          desc.streams().size() > 1u) {
        return RTC_LOG_ERROR(
            RTCError(RTCErrorType::INVALID_PARAMETER)
            << "Media section has more than one track specified "
               "with a=ssrc lines which is not supported with "
               "Unified Plan.");
      }
    }
    // Validate spec-simulcast which only works if the remote end negotiated the
    // mid and rid header extension.
    error = ValidateRtpHeaderExtensionsForSpecSimulcast(*sdesc->description());
    if (!error.ok()) {
      return error;
    }

    if (source == CS_REMOTE &&
        (type == SdpType::kPrAnswer || type == SdpType::kAnswer)) {
      RTC_DCHECK(local_description());
      error = VerifyDirectionsInAnswer(local_description()->description(),
                                       sdesc->description());
      if (!error.ok() && !env_.field_trials().IsDisabled(
                             "WebRTC-EnforceTransceiverDirection")) {
        return error;
      }
    }
  }

  // Validate Sframe consistency: reject remote answers that introduce
  // a=sframe for media sections where the local offer did not include it.
  if (source == CS_REMOTE &&
      (type == SdpType::kPrAnswer || type == SdpType::kAnswer)) {
    RTC_DCHECK(local_description());
    error = VerifySframeInAnswer(local_description()->description(),
                                 sdesc->description());
    if (!error.ok()) {
      return error;
    }
  }

  return RTCError::OK();
}

RTCError SdpOfferAnswerHandler::UpdateTransceiversAndDataChannels(
    ContentSource source,
    const SessionDescriptionInterface& new_session,
    const SessionDescriptionInterface* old_local_description,
    const SessionDescriptionInterface* old_remote_description,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  TRACE_EVENT0("webrtc",
               "SdpOfferAnswerHandler::UpdateTransceiversAndDataChannels");
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(IsUnifiedPlan());

  if (new_session.GetType() == SdpType::kOffer) {
    // If the BUNDLE policy is max-bundle, then we know for sure that all
    // transports will be bundled from the start. Return an error if
    // max-bundle is specified but the session description does not have a
    // BUNDLE group.
    if (pc_->configuration()->bundle_policy ==
            PeerConnectionInterface::kBundlePolicyMaxBundle &&
        bundle_groups_by_mid.empty()) {
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::INVALID_PARAMETER)
          << "max-bundle configured but session description has "
             "no BUNDLE group");
    }
  }

  // Thread safety requires specific execution order for tasks:
  // - Construction: Worker tasks before Network tasks.
  //   (Network depends on Worker setup).
  // - Destruction: Network tasks before Worker tasks.
  //   (Worker cleanup may depend on Network teardown).
  // The batchers below are declared and executed to enforce these orders.
  ScopedOperationsBatcher network_teardown_tasks(context_->network_thread());
  ScopedOperationsBatcher worker_tasks(context_->worker_thread());
  ScopedOperationsBatcher network_init_tasks(context_->network_thread());
  const ContentInfos& new_contents = new_session.description()->contents();
  struct TransceiverUpdate {
    scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>> transceiver;
    const ContentInfo& content;
  };
  std::vector<TransceiverUpdate> transceivers_to_update;

  for (size_t i = 0; i < new_contents.size(); ++i) {
    const ContentInfo& new_content = new_contents[i];
    MediaType media_type = new_content.media_description()->type();
    mid_generator_.AddKnownId(new_content.mid());
    if (media_type == MediaType::AUDIO || media_type == MediaType::VIDEO) {
      const ContentInfo* old_local_content = nullptr;
      if (old_local_description &&
          i < old_local_description->description()->contents().size()) {
        old_local_content =
            &old_local_description->description()->contents()[i];
      }
      const ContentInfo* old_remote_content = nullptr;
      if (old_remote_description &&
          i < old_remote_description->description()->contents().size()) {
        old_remote_content =
            &old_remote_description->description()->contents()[i];
      }
      auto transceiver_or_error =
          AssociateTransceiver(source, new_session.GetType(), i, new_content,
                               old_local_content, old_remote_content);
      if (!transceiver_or_error.ok()) {
        // In the case where a transceiver is rejected locally prior to being
        // associated, we don't expect to find a transceiver, but might find it
        // in the case where state is still "stopping", not "stopped".
        if (new_content.rejected) {
          continue;
        }
        return transceiver_or_error.MoveError();
      }
      transceivers_to_update.push_back(
          {transceiver_or_error.MoveValue(), new_content});
    } else if (media_type == MediaType::DATA) {
      const std::optional<std::string> data_mid = pc_->sctp_mid();
      if (data_mid && new_content.mid() != data_mid.value()) {
        // Ignore all but the first data section.
        RTC_LOG(LS_INFO) << "Ignoring data media section with MID="
                         << new_content.mid();
        continue;
      }
      auto it = bundle_groups_by_mid.find(new_content.mid());
      const ContentGroup* bundle_group =
          it != bundle_groups_by_mid.end() ? it->second : nullptr;
      RTCError error =
          UpdateDataChannelTransport(source, new_content, bundle_group);
      if (!error.ok()) {
        return error;
      }
    } else if (media_type == MediaType::UNSUPPORTED) {
      RTC_LOG(LS_INFO) << "Ignoring unsupported media type";
    } else {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                           << "Unknown section type.");
    }
  }

  for (TransceiverUpdate& update : transceivers_to_update) {
    auto it = bundle_groups_by_mid.find(update.content.mid());
    const ContentGroup* bundle_group =
        it != bundle_groups_by_mid.end() ? it->second : nullptr;

    UpdateTransceiverChannel(update.transceiver, update.content, bundle_group,
                             network_teardown_tasks, worker_tasks,
                             network_init_tasks);
    // Handle locally rejected content. This code path is only needed for apps
    // that SDP munge. Remote rejected content is handled in
    // ApplyRemoteDescriptionUpdateTransceiverState().
    // Do not use std::move here to ensure that the transceiver stays alive
    // in the `transceivers_to_update` vector until the end of this function.
    // This guarantees that it outlives the execution of `worker_tasks.Run()`.
    MaybeHandleLocallyRejectedTransceiver(source, new_session, update.content,
                                          update.transceiver, worker_tasks);
  }

  RTCError error = network_teardown_tasks.Run();
  RTC_DCHECK(error.ok());  // Teardown tasks cannot fail.
  error = worker_tasks.Run();
  RTC_DCHECK(error.ok());  // Cleanup and construction tasks cannot fail.
  return network_init_tasks.Run();
}

RTCErrorOr<scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>>
SdpOfferAnswerHandler::AssociateTransceiver(
    ContentSource source,
    SdpType type,
    size_t mline_index,
    const ContentInfo& content,
    const ContentInfo* old_local_content,
    const ContentInfo* old_remote_content) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::AssociateTransceiver");
  RTC_DCHECK(IsUnifiedPlan());
#if RTC_DCHECK_IS_ON
  // If this is an offer then the m= section might be recycled. If the m=
  // section is being recycled (defined as: rejected in the current local or
  // remote description and not rejected in new description), the transceiver
  // should have been removed by RemoveStoppedtransceivers()->
  if (IsMediaSectionBeingRecycled(type, content, old_local_content,
                                  old_remote_content)) {
    const auto old_mid = (old_local_content && old_local_content->rejected)
                             ? old_local_content->mid()
                             : old_remote_content->mid();
    auto old_transceiver = transceivers()->FindByMid(old_mid);
    // The transceiver should be disassociated in RemoveStoppedTransceivers()
    RTC_DCHECK(!old_transceiver);
  }
#endif

  bool newly_created = false;
  const MediaContentDescription* media_desc = content.media_description();
  std::vector<SimulcastLayer> initial_simulcast_layers;
  bool simulcast_rejected = GetInitialSimulcastLayersAndRejectionStatus(
      source, *media_desc, old_local_content,
      pc_->GetCryptoOptions().srtp.enable_encrypted_rtp_header_extensions,
      initial_simulcast_layers);

  auto transceiver = transceivers()->FindByMid(content.mid());
  if (source == CS_LOCAL) {
    // Find the RtpTransceiver that corresponds to this m= section, using the
    // mapping between transceivers and m= section indices established when
    // creating the offer.
    if (!transceiver) {
      // Only consider transceivers that are not yet associated. The channel of
      // an associated transceiver stays bound to the MID it was created for
      // (and to the transport of that MID), so the MID must not change.
      transceiver = transceivers()->FindUnassociatedByMLineIndex(mline_index);
    }
    if (!transceiver) {
      // This may happen normally when media sections are rejected.
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "Transceiver not found based on m-line index");
    }
  } else {
    RTC_DCHECK_EQ(source, CS_REMOTE);
    // If the m= section is sendrecv or recvonly, and there are RtpTransceivers
    // of the same type...
    // When simulcast is requested, a transceiver cannot be associated because
    // AddTrack cannot be called to initialize it.
    if (!transceiver &&
        RtpTransceiverDirectionHasRecv(media_desc->direction()) &&
        !media_desc->HasSimulcast()) {
      transceiver = FindAvailableTransceiverToReceive(media_desc->type());
    }

    // If no RtpTransceiver was found in the previous step, create one with a
    // recvonly direction.
    if (!transceiver) {
      newly_created = true;
      RTC_LOG(LS_INFO) << "Adding " << MediaTypeToString(media_desc->type())
                       << " transceiver for MID=" << content.mid()
                       << " at i=" << mline_index
                       << " in response to the remote description.";
      std::string sender_id = CreateRandomUuid();
      std::vector<RtpEncodingParameters> send_encodings =
          GetSendEncodingsFromRemoteDescription(*media_desc);
      std::string receiver_id;
      if (!media_desc->streams().empty()) {
        receiver_id = media_desc->streams()[0].id;
      } else {
        receiver_id = CreateRandomUuid();
      }
      // TODO: bugs.webrtc.org/42222804 - Batch up the worker thread blocking
      // call that's embedded in the construction of the transceiver.
      transceiver = rtp_manager()->CreateAndAddTransceiver(
          pc_->configuration()->media_config, audio_options_, video_options_,
          pc_->GetCryptoOptions(), video_bitrate_allocator_factory_.get(),
          media_desc->type(), nullptr, {}, send_encodings,
          /*header_extensions_to_negotiate=*/{}, simulcast_rejected,
          initial_simulcast_layers, sender_id, receiver_id);
      transceiver->internal()->set_direction(
          RtpTransceiverDirection::kRecvOnly);
      transceiver->internal()->ApplySframeEnabled(media_desc->sframe_enabled());
      if (type == SdpType::kOffer) {
        transceivers()->StableState(transceiver)->set_newly_created();
      }
    }

    RTC_DCHECK(transceiver);

    // Check if the offer indicated simulcast but the answer rejected it.
    // This can happen when simulcast is not supported on the remote party.
    if (!newly_created && simulcast_rejected) {
      // TODO: bugs.webrtc.org/42222804 - Optimize this path to avoid blocking
      // calls for existing transceivers.
      RTCError error =
          DisableSimulcastInSender(transceiver->internal()->sender_internal());
      if (!error.ok()) {
        RTC_LOG(LS_ERROR) << "Failed to remove rejected simulcast.";
        return std::move(error);
      }
    }
  }

  if (transceiver->media_type() != media_desc->type()) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INVALID_PARAMETER)
        << "Transceiver type does not match media description type.");
  }

  if (!newly_created && media_desc->HasSimulcast()) {
    // TODO: bugs.webrtc.org/42222804 - Optimize this path to avoid blocking
    // calls for existing transceivers.
    RTCError error = UpdateSimulcastLayerStatusInSender(
        initial_simulcast_layers, transceiver->internal()->sender_internal());
    if (!error.ok()) {
      RTC_LOG(LS_ERROR) << "Failed updating status for simulcast layers.";
      return std::move(error);
    }
  }
  if (type == SdpType::kOffer) {
    bool state_changes = transceiver->internal()->mid() != content.mid() ||
                         transceiver->internal()->mline_index() != mline_index;
    if (state_changes) {
      transceivers()
          ->StableState(transceiver)
          ->SetMSectionIfUnset(transceiver->internal()->mid(),
                               transceiver->internal()->mline_index());
    }
  }
  // Associate the found or created RtpTransceiver with the m= section by
  // setting the value of the RtpTransceiver's mid property to the MID of the m=
  // section, and establish a mapping between the transceiver and the index of
  // the m= section.
  transceiver->internal()->set_mid(std::string(content.mid()));
  transceiver->internal()->set_mline_index(mline_index);
  return std::move(transceiver);
}

void SdpOfferAnswerHandler::UpdateTransceiverChannel(
    scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>> transceiver,
    const ContentInfo& content,
    const ContentGroup* bundle_group,
    ScopedOperationsBatcher& network_teardown_tasks,
    ScopedOperationsBatcher& worker_tasks,
    ScopedOperationsBatcher& network_init_tasks) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::UpdateTransceiverChannel");
  RTC_DCHECK(IsUnifiedPlan());
  RTC_DCHECK(transceiver);
  // A stopped transceiver must not have a channel.
  if (content.rejected || transceiver->internal()->stopped()) {
    if (transceiver->internal()->HasChannel()) {
      network_teardown_tasks.Add(
          transceiver->internal()->GetClearChannelNetworkTask());
      worker_tasks.Add(transceiver->internal()->GetDeleteChannelWorkerTask(
          /*stop_senders=*/false));
    }
  } else {
    if (!transceiver->internal()->HasChannel()) {
      transceiver->internal()->CreateChannel(
          content.mid(), pc_->call_ptr(), pc_->configuration()->media_config,
          pc_->SrtpRequired(), pc_->GetCryptoOptions(),
          video_bitrate_allocator_factory_.get(),
          [mid = content.mid(), this]() {
            RTC_DCHECK_RUN_ON(network_thread());
            return transport_controller_n()->GetRtpTransport(mid);
          },
          worker_tasks, network_init_tasks);
    }
  }
}

RTCError SdpOfferAnswerHandler::UpdateDataChannelTransport(
    ContentSource source,
    const ContentInfo& content,
    const ContentGroup* bundle_group) {
  if (content.rejected) {
    RTC_LOG(LS_INFO) << "Rejected data channel transport with mid="
                     << content.mid();

    StringBuilder sb;
    sb << "Rejected data channel transport with mid=" << content.mid();
    RTCError error(RTCErrorType::OPERATION_ERROR_WITH_DATA, sb.Release());
    error.set_error_detail(RTCErrorDetailType::DATA_CHANNEL_FAILURE);
    pc_->DestroyDataChannelTransport(error);
  } else if (!pc_->CreateDataChannelTransport(content.mid())) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to create data channel.");
  }
  return RTCError::OK();
}

bool SdpOfferAnswerHandler::ExpectSetLocalDescription(SdpType type) {
  PeerConnectionInterface::SignalingState state = signaling_state();
  if (type == SdpType::kOffer) {
    return (state == PeerConnectionInterface::kStable) ||
           (state == PeerConnectionInterface::kHaveLocalOffer);
  } else {
    RTC_DCHECK(type == SdpType::kPrAnswer || type == SdpType::kAnswer);
    return (state == PeerConnectionInterface::kHaveRemoteOffer) ||
           (state == PeerConnectionInterface::kHaveLocalPrAnswer);
  }
}

bool SdpOfferAnswerHandler::ExpectSetRemoteDescription(SdpType type) {
  PeerConnectionInterface::SignalingState state = signaling_state();
  if (type == SdpType::kOffer) {
    return (state == PeerConnectionInterface::kStable) ||
           (state == PeerConnectionInterface::kHaveRemoteOffer);
  } else {
    RTC_DCHECK(type == SdpType::kPrAnswer || type == SdpType::kAnswer);
    return (state == PeerConnectionInterface::kHaveLocalOffer) ||
           (state == PeerConnectionInterface::kHaveRemotePrAnswer);
  }
}

void SdpOfferAnswerHandler::FillInMissingRemoteMids(
    SessionDescription* new_remote_description) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(new_remote_description);
  const ContentInfos no_infos;
  const ContentInfos& local_contents =
      (local_description() ? local_description()->description()->contents()
                           : no_infos);
  const ContentInfos& remote_contents =
      (remote_description() ? remote_description()->description()->contents()
                            : no_infos);
  for (size_t i = 0; i < new_remote_description->contents().size(); ++i) {
    ContentInfo& content = new_remote_description->contents()[i];
    if (!content.mid().empty()) {
      continue;
    }
    std::string new_mid;
    absl::string_view source_explanation;
    if (IsUnifiedPlan()) {
      if (i < local_contents.size()) {
        new_mid = local_contents[i].mid();
        source_explanation = "from the matching local media section";
      } else if (i < remote_contents.size()) {
        new_mid = remote_contents[i].mid();
        source_explanation = "from the matching previous remote media section";
      } else {
        new_mid = mid_generator_.GenerateString();
        source_explanation = "generated just now";
      }
    } else {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
      new_mid = GetDefaultMidForPlanB(content.media_description()->type());
      RTC_ALLOW_PLAN_B_DEPRECATION_END();
      source_explanation = "to match pre-existing behavior";
    }
    RTC_DCHECK(!new_mid.empty());
    content.set_mid(new_mid);
    new_remote_description->transport_infos()[i].content_name = new_mid;
    RTC_LOG(LS_INFO) << "SetRemoteDescription: Remote media section at i=" << i
                     << " is missing an a=mid line. Filling in the value '"
                     << new_mid << "' " << source_explanation << ".";
  }
}

scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
SdpOfferAnswerHandler::FindAvailableTransceiverToReceive(
    MediaType media_type) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(IsUnifiedPlan());
  // From JSEP section 5.10 (Applying a Remote Description):
  // If the m= section is sendrecv or recvonly, and there are RtpTransceivers of
  // the same type that were added to the PeerConnection by addTrack and are not
  // associated with any m= section and are not stopped, find the first such
  // RtpTransceiver.
  for (const auto& transceiver : transceivers()->List()) {
    if (transceiver->media_type() == media_type &&
        transceiver->internal()->created_by_addtrack() && !transceiver->mid() &&
        !transceiver->stopped()) {
      return transceiver;
    }
  }
  return nullptr;
}

const ContentInfo* SdpOfferAnswerHandler::FindMediaSectionForTransceiver(
    const RtpTransceiver* transceiver,
    const SessionDescriptionInterface* sdesc) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(transceiver);
  RTC_DCHECK(sdesc);
  if (IsUnifiedPlan()) {
    if (!transceiver->mid()) {
      // This transceiver is not associated with a media section yet.
      return nullptr;
    }
    return sdesc->description()->GetContentByName(*transceiver->mid());
  } else {
    // Plan B only allows at most one audio and one video section, so use the
    // first media section of that type.
    return GetFirstMediaContent(sdesc->description()->contents(),
                                transceiver->media_type());
  }
}

void SdpOfferAnswerHandler::GetOptionsForOffer(
    const PeerConnectionInterface::RTCOfferAnswerOptions& offer_answer_options,
    MediaSessionOptions* session_options) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  ExtractSharedMediaSessionOptions(offer_answer_options, session_options);

  if (IsUnifiedPlan()) {
    GetOptionsForUnifiedPlanOffer(offer_answer_options, session_options);
  } else {
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    GetOptionsForPlanBOffer(offer_answer_options, session_options);
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
  }

  // Apply ICE restart flag and renomination and dtls-in-stun ICE options.
  bool ice_restart = offer_answer_options.ice_restart || HasNewIceCredentials();
  for (auto& options : session_options->media_description_options) {
    options.transport_options.ice_restart = ice_restart;
    options.transport_options.enable_ice_renomination =
        pc_->configuration()->enable_ice_renomination;
    options.transport_options.dtls_handshake_in_stun =
        pc_->CanAttemptDtlsStunPiggybacking();
  }

  session_options->rtcp_cname = rtcp_cname_;
  session_options->crypto_options = pc_->GetCryptoOptions();
  session_options->pooled_ice_credentials = cached_pooled_ice_credentials_;
  session_options->offer_extmap_allow_mixed =
      pc_->configuration()->offer_extmap_allow_mixed;

  // Allow fallback for using obsolete SCTP syntax.
  // Note that the default in `session_options` is true, while
  // the default in `options` is false.
  session_options->use_obsolete_sctp_sdp =
      offer_answer_options.use_obsolete_sctp_sdp;
  // draft-hancke-tsvwg-snap.
  session_options->use_sctp_snap = pc_->configuration()->enable_sctp_snap;
}

void SdpOfferAnswerHandler::GetOptionsForPlanBOffer(
    const PeerConnectionInterface::RTCOfferAnswerOptions& offer_answer_options,
    MediaSessionOptions* session_options) {
  bool offer_new_data_description =
      data_channel_controller()->HasUsedDataChannels();
  bool send_audio = false;
  bool send_video = false;
  bool recv_audio = false;
  bool recv_video = false;
  if (ConfiguredForMedia()) {
    // Figure out transceiver directional preferences.
    send_audio =
        !rtp_manager()->GetAudioTransceiver()->internal()->senders().empty();
    send_video =
        !rtp_manager()->GetVideoTransceiver()->internal()->senders().empty();

    // By default, generate sendrecv/recvonly m= sections.
    recv_audio = true;
    recv_video = true;
  }
  // By default, only offer a new m= section if we have media to send with it.
  bool offer_new_audio_description = send_audio;
  bool offer_new_video_description = send_video;
  if (ConfiguredForMedia()) {
    // The "offer_to_receive_X" options allow those defaults to be overridden.
    if (offer_answer_options.offer_to_receive_audio !=
        PeerConnectionInterface::RTCOfferAnswerOptions::kUndefined) {
      recv_audio = (offer_answer_options.offer_to_receive_audio > 0);
      offer_new_audio_description =
          offer_new_audio_description ||
          (offer_answer_options.offer_to_receive_audio > 0);
    }
    if (offer_answer_options.offer_to_receive_video !=
        RTCOfferAnswerOptions::kUndefined) {
      recv_video = (offer_answer_options.offer_to_receive_video > 0);
      offer_new_video_description =
          offer_new_video_description ||
          (offer_answer_options.offer_to_receive_video > 0);
    }
  }
  std::optional<size_t> audio_index;
  std::optional<size_t> video_index;
  std::optional<size_t> data_index;
  // If a current description exists, generate m= sections in the same order,
  // using the first audio/video/data section that appears and rejecting
  // extraneous ones.
  if (local_description()) {
    GenerateMediaDescriptionOptions(
        local_description(),
        RtpTransceiverDirectionFromSendRecv(send_audio, recv_audio),
        RtpTransceiverDirectionFromSendRecv(send_video, recv_video),
        &audio_index, &video_index, &data_index, session_options);
  }

  if (ConfiguredForMedia()) {
    // Add audio/video/data m= sections to the end if needed.
    if (!audio_index && offer_new_audio_description) {
      MediaDescriptionOptions options(
          MediaType::AUDIO, GetDefaultMidForPlanB(MediaType::AUDIO),
          RtpTransceiverDirectionFromSendRecv(send_audio, recv_audio), false);
      options.header_extensions =
          media_engine()->voice().GetRtpHeaderExtensions(&pc_->trials());
      session_options->media_description_options.push_back(options);
      audio_index = session_options->media_description_options.size() - 1;
    }
    if (!video_index && offer_new_video_description) {
      MediaDescriptionOptions options(
          MediaType::VIDEO, GetDefaultMidForPlanB(MediaType::VIDEO),
          RtpTransceiverDirectionFromSendRecv(send_video, recv_video), false);
      options.header_extensions =
          media_engine()->video().GetRtpHeaderExtensions(&pc_->trials());
      session_options->media_description_options.push_back(options);
      video_index = session_options->media_description_options.size() - 1;
    }
    MediaDescriptionOptions* audio_media_description_options =
        !audio_index
            ? nullptr
            : &session_options->media_description_options[*audio_index];
    MediaDescriptionOptions* video_media_description_options =
        !video_index
            ? nullptr
            : &session_options->media_description_options[*video_index];

    AddPlanBRtpSenderOptions(rtp_manager()->GetSendersInternal(),
                             audio_media_description_options,
                             video_media_description_options,
                             offer_answer_options.num_simulcast_layers);
  }
  if (!data_index && offer_new_data_description) {
    session_options->media_description_options.push_back(
        GetMediaDescriptionOptionsForActiveData(
            GetDefaultMidForPlanB(MediaType::DATA)));
  }
}

void SdpOfferAnswerHandler::GetOptionsForUnifiedPlanOffer(
    const RTCOfferAnswerOptions& offer_answer_options,
    MediaSessionOptions* session_options) {
  // Rules for generating an offer are dictated by JSEP sections 5.2.1 (Initial
  // Offers) and 5.2.2 (Subsequent Offers).
  RTC_DCHECK_EQ(session_options->media_description_options.size(), 0);
  const ContentInfos no_infos;
  const ContentInfos& local_contents =
      (local_description() ? local_description()->description()->contents()
                           : no_infos);
  const ContentInfos& remote_contents =
      (remote_description() ? remote_description()->description()->contents()
                            : no_infos);
  // The mline indices that can be recycled. New transceivers should reuse these
  // slots first.
  std::queue<size_t> recycleable_mline_indices;
  // First, go through each media section that exists in either the local or
  // remote description and generate a media section in this offer for the
  // associated transceiver. If a media section can be recycled, generate a
  // default, rejected media section here that can be later overwritten.
  for (size_t i = 0;
       i < std::max(local_contents.size(), remote_contents.size()); ++i) {
    // Either `local_content` or `remote_content` is non-null.
    const ContentInfo* local_content =
        (i < local_contents.size() ? &local_contents[i] : nullptr);
    const ContentInfo* current_local_content =
        GetContentByIndex(current_local_description(), i);
    const ContentInfo* remote_content =
        (i < remote_contents.size() ? &remote_contents[i] : nullptr);
    const ContentInfo* current_remote_content =
        GetContentByIndex(current_remote_description(), i);
    bool had_been_rejected =
        (current_local_content && current_local_content->rejected) ||
        (current_remote_content && current_remote_content->rejected);
    const absl::string_view mid =
        local_content ? local_content->mid() : remote_content->mid();
    MediaType media_type =
        (local_content ? local_content->media_description()->type()
                       : remote_content->media_description()->type());
    if (media_type == MediaType::AUDIO || media_type == MediaType::VIDEO) {
      // A media section is considered eligible for recycling if it is marked as
      // rejected in either the current local or current remote description.
      auto transceiver = transceivers()->FindByMid(mid);
      if (!transceiver) {
        // No associated transceiver. The media section has been stopped.
        recycleable_mline_indices.push(i);
        session_options->media_description_options.push_back(
            MediaDescriptionOptions(media_type, mid,
                                    RtpTransceiverDirection::kInactive,
                                    /*stopped=*/true));
      } else {
        // NOTE: a stopping transceiver should be treated as a stopped one in
        // createOffer as specified in
        // https://w3c.github.io/webrtc-pc/#dom-rtcpeerconnection-createoffer.
        if (had_been_rejected && transceiver->stopping()) {
          session_options->media_description_options.push_back(
              MediaDescriptionOptions(transceiver->media_type(), mid,
                                      RtpTransceiverDirection::kInactive,
                                      /*stopped=*/true));
          recycleable_mline_indices.push(i);
        } else {
          session_options->media_description_options.push_back(
              GetMediaDescriptionOptionsForTransceiver(
                  transceiver->internal(), mid,
                  /*is_create_offer=*/true));
          // CreateOffer shouldn't really cause any state changes in
          // PeerConnection, but we need a way to match new transceivers to new
          // media sections in SetLocalDescription and JSEP specifies this is
          // done by recording the index of the media section generated for the
          // transceiver in the offer.
          transceiver->internal()->set_mline_index(i);
        }
      }
    } else if (media_type == MediaType::UNSUPPORTED) {
      RTC_DCHECK(local_content->rejected);
      session_options->media_description_options.push_back(
          MediaDescriptionOptions(media_type, mid,
                                  RtpTransceiverDirection::kInactive,
                                  /*stopped=*/true));
    } else {
      RTC_CHECK_EQ(MediaType::DATA, media_type);
      if (had_been_rejected) {
        session_options->media_description_options.push_back(
            GetMediaDescriptionOptionsForRejectedData(mid));
      } else {
        const auto data_mid = pc_->sctp_mid();
        if (data_mid.has_value() && mid == data_mid.value()) {
          session_options->media_description_options.push_back(
              GetMediaDescriptionOptionsForActiveData(mid));
        } else {
          if (!data_mid.has_value()) {
            RTC_LOG(LS_ERROR) << "Datachannel transport not available: " << mid;
          }
          session_options->media_description_options.push_back(
              GetMediaDescriptionOptionsForRejectedData(mid));
        }
      }
    }
  }

  // Add a datachannel m-line first if explicitly asked even when no data
  // channels exist.
  if (pc_->configuration()->always_negotiate_data_channels &&
      !pc_->sctp_mid()) {
    MaybeNegotiateSctp(session_options);
  }

  // Next, look for transceivers that are newly added (that is, are not stopped
  // and not associated). Reuse media sections marked as recyclable first,
  // otherwise append to the end of the offer. New media sections should be
  // added in the order they were added to the PeerConnection.
  if (ConfiguredForMedia()) {
    for (const auto& transceiver : transceivers()->ListInternal()) {
      if (transceiver->mid() || transceiver->stopping()) {
        continue;
      }
      size_t mline_index;
      if (!recycleable_mline_indices.empty()) {
        mline_index = recycleable_mline_indices.front();
        recycleable_mline_indices.pop();
        session_options->media_description_options[mline_index] =
            GetMediaDescriptionOptionsForTransceiver(
                transceiver, mid_generator_.GenerateString(),
                /*is_create_offer=*/true);
      } else {
        mline_index = session_options->media_description_options.size();
        session_options->media_description_options.push_back(
            GetMediaDescriptionOptionsForTransceiver(
                transceiver, mid_generator_.GenerateString(),
                /*is_create_offer=*/true));
      }
      // See comment above for why CreateOffer changes the transceiver's state.
      transceiver->set_mline_index(mline_index);
    }
  }
  // Lastly, add a m-section if we have requested local data channels and an
  // m section does not already exist.
  if (!pc_->configuration()->always_negotiate_data_channels &&
      !pc_->sctp_mid() && data_channel_controller()->HasDataChannels()) {
    MaybeNegotiateSctp(session_options);
  }
}

void SdpOfferAnswerHandler::MaybeNegotiateSctp(
    MediaSessionOptions* session_options) {
  // Attempt to recycle a stopped m-line.
  // TODO(crbug.com/1442604): sctp_mid() should return the mid if one was
  // ever created but rejected.
  for (size_t i = 0; i < session_options->media_description_options.size();
       i++) {
    auto media_description = session_options->media_description_options[i];
    if (media_description.type == MediaType::DATA &&
        media_description.stopped) {
      session_options->media_description_options[i] =
          GetMediaDescriptionOptionsForActiveData(media_description.mid);
      return;
    }
  }
  // Generate a new m-line.
  session_options->media_description_options.push_back(
      GetMediaDescriptionOptionsForActiveData(mid_generator_.GenerateString()));
}

void SdpOfferAnswerHandler::GetOptionsForAnswer(
    const RTCOfferAnswerOptions& offer_answer_options,
    MediaSessionOptions* session_options) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  ExtractSharedMediaSessionOptions(offer_answer_options, session_options);

  if (IsUnifiedPlan()) {
    GetOptionsForUnifiedPlanAnswer(offer_answer_options, session_options);
  } else {
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    GetOptionsForPlanBAnswer(offer_answer_options, session_options);
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
  }

  // Apply renomination and dtls-in-stun ICE options.
  for (auto& options : session_options->media_description_options) {
    options.transport_options.enable_ice_renomination =
        pc_->configuration()->enable_ice_renomination;
    options.transport_options.dtls_handshake_in_stun =
        pc_->CanAttemptDtlsStunPiggybacking();
  }

  session_options->rtcp_cname = rtcp_cname_;
  session_options->crypto_options = pc_->GetCryptoOptions();
  session_options->pooled_ice_credentials = cached_pooled_ice_credentials_;
  // draft-hancke-tsvwg-snap.
  session_options->use_sctp_snap = pc_->configuration()->enable_sctp_snap;
}

void SdpOfferAnswerHandler::GetOptionsForPlanBAnswer(
    const PeerConnectionInterface::RTCOfferAnswerOptions& offer_answer_options,
    MediaSessionOptions* session_options) {
  bool send_audio = false;
  bool recv_audio = false;
  bool send_video = false;
  bool recv_video = false;

  if (ConfiguredForMedia()) {
    // Figure out transceiver directional preferences.
    send_audio =
        !rtp_manager()->GetAudioTransceiver()->internal()->senders().empty();
    send_video =
        !rtp_manager()->GetVideoTransceiver()->internal()->senders().empty();

    // By default, generate sendrecv/recvonly m= sections. The direction is also
    // restricted by the direction in the offer.
    recv_audio = true;
    recv_video = true;

    // The "offer_to_receive_X" options allow those defaults to be overridden.
    if (offer_answer_options.offer_to_receive_audio !=
        RTCOfferAnswerOptions::kUndefined) {
      recv_audio = (offer_answer_options.offer_to_receive_audio > 0);
    }
    if (offer_answer_options.offer_to_receive_video !=
        RTCOfferAnswerOptions::kUndefined) {
      recv_video = (offer_answer_options.offer_to_receive_video > 0);
    }
  }

  std::optional<size_t> audio_index;
  std::optional<size_t> video_index;
  std::optional<size_t> data_index;

  // Generate m= sections that match those in the offer.
  // Note that mediasession.cc will handle intersection our preferred
  // direction with the offered direction.
  GenerateMediaDescriptionOptions(
      remote_description(),
      RtpTransceiverDirectionFromSendRecv(send_audio, recv_audio),
      RtpTransceiverDirectionFromSendRecv(send_video, recv_video), &audio_index,
      &video_index, &data_index, session_options);

  MediaDescriptionOptions* audio_media_description_options =
      !audio_index ? nullptr
                   : &session_options->media_description_options[*audio_index];
  MediaDescriptionOptions* video_media_description_options =
      !video_index ? nullptr
                   : &session_options->media_description_options[*video_index];

  if (ConfiguredForMedia()) {
    AddPlanBRtpSenderOptions(rtp_manager()->GetSendersInternal(),
                             audio_media_description_options,
                             video_media_description_options,
                             offer_answer_options.num_simulcast_layers);
  }
}

void SdpOfferAnswerHandler::GetOptionsForUnifiedPlanAnswer(
    const PeerConnectionInterface::RTCOfferAnswerOptions& offer_answer_options,
    MediaSessionOptions* session_options) {
  // Rules for generating an answer are dictated by JSEP sections 5.3.1 (Initial
  // Answers) and 5.3.2 (Subsequent Answers).
  RTC_DCHECK(remote_description());
  RTC_DCHECK(remote_description()->GetType() == SdpType::kOffer);
  for (const ContentInfo& content :
       remote_description()->description()->contents()) {
    MediaType media_type = content.media_description()->type();
    if (media_type == MediaType::AUDIO || media_type == MediaType::VIDEO) {
      auto transceiver = transceivers()->FindByMid(content.mid());
      if (transceiver) {
        session_options->media_description_options.push_back(
            GetMediaDescriptionOptionsForTransceiver(
                transceiver->internal(), content.mid(),
                /*is_create_offer=*/false));
      } else {
        // This should only happen with rejected transceivers.
        RTC_DCHECK(content.rejected);
        session_options->media_description_options.push_back(
            MediaDescriptionOptions(media_type, content.mid(),
                                    RtpTransceiverDirection::kInactive,
                                    /*stopped=*/true));
      }
    } else if (media_type == MediaType::UNSUPPORTED) {
      RTC_DCHECK(content.rejected);
      session_options->media_description_options.push_back(
          MediaDescriptionOptions(media_type, content.mid(),
                                  RtpTransceiverDirection::kInactive,
                                  /*stopped=*/true));
    } else {
      RTC_CHECK_EQ(MediaType::DATA, media_type);
      // Reject all data sections if data channels are disabled.
      // Reject a data section if it has already been rejected.
      // Reject all data sections except for the first one.
      auto sctp_mid = pc_->sctp_mid();
      if (content.rejected ||
          (sctp_mid.has_value() && content.mid() != *sctp_mid)) {
        session_options->media_description_options.push_back(
            GetMediaDescriptionOptionsForRejectedData(content.mid()));
      } else {
        session_options->media_description_options.push_back(
            GetMediaDescriptionOptionsForActiveData(content.mid()));
      }
    }
  }
}

const char* SdpOfferAnswerHandler::SessionErrorToString(
    SessionError error) const {
  switch (error) {
    case SessionError::kNone:
      return "ERROR_NONE";
    case SessionError::kContent:
      return "ERROR_CONTENT";
    case SessionError::kTransport:
      return "ERROR_TRANSPORT";
  }
  RTC_DCHECK_NOTREACHED();
  return "";
}

std::string SdpOfferAnswerHandler::GetSessionErrorMsg() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  StringBuilder desc;
  desc << kSessionError << SessionErrorToString(session_error()) << ". ";
  desc << kSessionErrorDesc << session_error_desc() << ".";
  return desc.Release();
}

void SdpOfferAnswerHandler::SetSessionError(SessionError error,
                                            const std::string& error_desc) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (error != session_error_) {
    session_error_ = error;
    session_error_desc_ = error_desc;
  }
}

RTCError SdpOfferAnswerHandler::HandleLegacyOfferOptions(
    const PeerConnectionInterface::RTCOfferAnswerOptions& options) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(IsUnifiedPlan());

  if (options.offer_to_receive_audio == 0) {
    RemoveRecvDirectionFromReceivingTransceiversOfType(MediaType::AUDIO);
  } else if (options.offer_to_receive_audio == 1) {
    AddUpToOneReceivingTransceiverOfType(MediaType::AUDIO);
  } else if (options.offer_to_receive_audio > 1) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_PARAMETER)
                         << "offer_to_receive_audio > 1 is not supported.");
  }

  if (options.offer_to_receive_video == 0) {
    RemoveRecvDirectionFromReceivingTransceiversOfType(MediaType::VIDEO);
  } else if (options.offer_to_receive_video == 1) {
    AddUpToOneReceivingTransceiverOfType(MediaType::VIDEO);
  } else if (options.offer_to_receive_video > 1) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_PARAMETER)
                         << "offer_to_receive_video > 1 is not supported.");
  }

  return RTCError::OK();
}

void SdpOfferAnswerHandler::RemoveRecvDirectionFromReceivingTransceiversOfType(
    MediaType media_type) {
  for (const auto& transceiver : GetReceivingTransceiversOfType(media_type)) {
    RtpTransceiverDirection new_direction =
        RtpTransceiverDirectionWithRecvSet(transceiver->direction(), false);
    if (new_direction != transceiver->direction()) {
      RTC_LOG(LS_INFO) << "Changing " << MediaTypeToString(media_type)
                       << " transceiver (MID="
                       << transceiver->mid().value_or("<not set>") << ") from "
                       << RtpTransceiverDirectionToString(
                              transceiver->direction())
                       << " to "
                       << RtpTransceiverDirectionToString(new_direction)
                       << " since CreateOffer specified offer_to_receive=0";
      transceiver->internal()->set_direction(new_direction);
    }
  }
}

void SdpOfferAnswerHandler::AddUpToOneReceivingTransceiverOfType(
    MediaType media_type) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (GetReceivingTransceiversOfType(media_type).empty()) {
    RTC_LOG(LS_INFO)
        << "Adding one recvonly " << MediaTypeToString(media_type)
        << " transceiver since CreateOffer specified offer_to_receive=1";
    RtpTransceiverInit init;
    init.direction = RtpTransceiverDirection::kRecvOnly;
    pc_->AddTransceiver(media_type, nullptr, init,
                        /*update_negotiation_needed=*/false);
  }
}

std::vector<scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>>
SdpOfferAnswerHandler::GetReceivingTransceiversOfType(MediaType media_type) {
  std::vector<scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>>
      receiving_transceivers;
  for (const auto& transceiver : transceivers()->List()) {
    if (!transceiver->stopped() && transceiver->media_type() == media_type &&
        RtpTransceiverDirectionHasRecv(transceiver->direction())) {
      receiving_transceivers.push_back(transceiver);
    }
  }
  return receiving_transceivers;
}

void SdpOfferAnswerHandler::ProcessRemovalOfRemoteTrack(
    scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>> transceiver,
    std::vector<scoped_refptr<RtpTransceiverInterface>>* remove_list,
    std::vector<scoped_refptr<MediaStreamInterface>>* removed_streams) {
  RTC_DCHECK(transceiver->mid());
  RTC_LOG(LS_INFO) << "Processing the removal of a track for MID="
                   << *transceiver->mid();
  std::vector<scoped_refptr<MediaStreamInterface>> previous_streams =
      transceiver->internal()->receiver_internal()->streams();
  // This will remove the remote track from the streams.
  transceiver->internal()->receiver_internal()->set_stream_ids({});
  remove_list->push_back(transceiver);
  RemoveRemoteStreamsIfEmpty(previous_streams, removed_streams);
}

void SdpOfferAnswerHandler::RemoveRemoteStreamsIfEmpty(
    const std::vector<scoped_refptr<MediaStreamInterface>>& remote_streams,
    std::vector<scoped_refptr<MediaStreamInterface>>* removed_streams) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // TODO(https://crbug.com/webrtc/9480): When we use stream IDs instead of
  // streams, see if the stream was removed by checking if this was the last
  // receiver with that stream ID.
  for (const auto& remote_stream : remote_streams) {
    if (remote_stream->GetAudioTracks().empty() &&
        remote_stream->GetVideoTracks().empty()) {
      remote_streams_->RemoveStream(remote_stream.get());
      removed_streams->push_back(remote_stream);
    }
  }
}

void SdpOfferAnswerHandler::RemoveSenders(MediaType media_type) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  UpdateLocalSendersPlanB(std::vector<StreamParams>(), media_type);
  UpdateRemoteSendersListPlanB(std::vector<StreamParams>(), false, media_type,
                               nullptr);
}

void SdpOfferAnswerHandler::UpdateLocalSendersPlanB(
    const std::vector<StreamParams>& streams,
    MediaType media_type) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::UpdateLocalSendersPlanB");
  RTC_DCHECK(!IsUnifiedPlan());
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<RtpSenderInfo>* current_senders =
      rtp_manager()->GetLocalSenderInfos(media_type);

  // Find removed tracks. I.e., tracks where the track id, stream id or ssrc
  // don't match the new StreamParam.
  for (auto sender_it = current_senders->begin();
       sender_it != current_senders->end();
       /* incremented manually */) {
    const RtpSenderInfo& info = *sender_it;
    const StreamParams* params = GetStreamBySsrc(streams, info.first_ssrc);
    if (!params || params->id != info.sender_id ||
        params->first_stream_id() != info.stream_id) {
      rtp_manager()->OnLocalSenderRemoved(info, media_type);
      sender_it = current_senders->erase(sender_it);
    } else {
      ++sender_it;
    }
  }

  // Find new and active senders.
  for (const StreamParams& params : streams) {
    // The sync_label is the MediaStream label and the `stream.id` is the
    // sender id.
    bool found = absl::c_any_of(*current_senders, [&](const auto& info) {
      return info.stream_id == params.first_stream_id() &&
             info.sender_id == params.id;
    });
    if (!found) {
      current_senders->push_back(RtpSenderInfo(params.first_stream_id(),
                                               params.id, params.first_ssrc()));
      rtp_manager()->OnLocalSenderAdded(current_senders->back(), media_type);
    }
  }
}

void SdpOfferAnswerHandler::UpdateRemoteSendersListPlanB(
    const StreamParamsVec& streams,
    bool default_sender_needed,
    MediaType media_type,
    StreamCollection* new_streams) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::UpdateRemoteSendersListPlanB");
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());

  std::vector<RtpSenderInfo>* current_senders =
      rtp_manager()->GetRemoteSenderInfos(media_type);

  // Find removed senders. I.e., senders where the sender id or ssrc don't match
  // the new StreamParam.
  for (auto sender_it = current_senders->begin();
       sender_it != current_senders->end();
       /* incremented manually */) {
    const RtpSenderInfo& info = *sender_it;
    const StreamParams* params = GetStreamBySsrc(streams, info.first_ssrc);
    std::string params_stream_id;
    if (params) {
      params_stream_id =
          (!params->first_stream_id().empty() ? params->first_stream_id()
                                              : kDefaultStreamId);
    }
    bool sender_exists = params && params->id == info.sender_id &&
                         params_stream_id == info.stream_id;
    // If this is a default track, and we still need it, don't remove it.
    if ((info.stream_id == kDefaultStreamId && default_sender_needed) ||
        sender_exists) {
      ++sender_it;
    } else {
      rtp_manager()->OnRemoteSenderRemovedPlanB(
          info, remote_streams_->find(info.stream_id), media_type);
      sender_it = current_senders->erase(sender_it);
    }
  }

  // Find new and active senders.
  for (const StreamParams& params : streams) {
    if (!params.has_ssrcs()) {
      // The remote endpoint has streams, but didn't signal ssrcs. For an active
      // sender, this means it is coming from a Unified Plan endpoint,so we just
      // create a default.
      default_sender_needed = true;
      break;
    }

    // `params.id` is the sender id and the stream id uses the first of
    // `params.stream_ids`. The remote description could come from a Unified
    // Plan endpoint, with multiple or no stream_ids() signaled. Since this is
    // not supported in Plan B, we just take the first here and create the
    // default stream ID if none is specified.
    std::string stream_id = (!params.first_stream_id().empty()
                                 ? std::string(params.first_stream_id())
                                 : kDefaultStreamId);
    const std::string& sender_id = params.id;
    uint32_t ssrc = params.first_ssrc();

    scoped_refptr<MediaStreamInterface> stream(
        remote_streams_->find(stream_id));
    if (!stream) {
      // This is a new MediaStream. Create a new remote MediaStream.
      stream = MediaStreamProxy::Create(Thread::Current(),
                                        MediaStream::Create(stream_id));
      remote_streams_->AddStream(stream);
      new_streams->AddStream(stream);
    }
    bool found = absl::c_any_of(*current_senders, [&](const auto& info) {
      return info.stream_id == stream_id && info.sender_id == sender_id;
    });
    if (!found) {
      current_senders->push_back(RtpSenderInfo(stream_id, sender_id, ssrc));
      rtp_manager()->OnRemoteSenderAddedPlanB(current_senders->back(),
                                              stream.get(), media_type);
    }
  }

  // Add default sender if necessary.
  if (default_sender_needed) {
    scoped_refptr<MediaStreamInterface> default_stream(
        remote_streams_->find(kDefaultStreamId));
    if (!default_stream) {
      // Create the new default MediaStream.
      default_stream = MediaStreamProxy::Create(
          Thread::Current(), MediaStream::Create(kDefaultStreamId));
      remote_streams_->AddStream(default_stream);
      new_streams->AddStream(default_stream);
    }
    absl::string_view default_sender_id = (media_type == MediaType::AUDIO)
                                              ? kDefaultAudioSenderId
                                              : kDefaultVideoSenderId;
    bool found = absl::c_any_of(*current_senders, [&](const auto& info) {
      return info.stream_id == kDefaultStreamId &&
             info.sender_id == default_sender_id;
    });

    if (!found) {
      current_senders->push_back(
          RtpSenderInfo(kDefaultStreamId, default_sender_id, /*ssrc=*/0));
      rtp_manager()->OnRemoteSenderAddedPlanB(current_senders->back(),
                                              default_stream.get(), media_type);
    }
  }
}

void SdpOfferAnswerHandler::EnableSending() {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::EnableSending");
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!ConfiguredForMedia()) {
    return;
  }
  for (const auto& transceiver : transceivers()->ListInternal()) {
    if (transceiver->HasChannel()) {
      transceiver->EnableChannel(true);
    }
  }
}

RTCError SdpOfferAnswerHandler::PushdownMediaDescription(
    SdpType type,
    ContentSource source,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::PushdownMediaDescription");
  RTC_DCHECK_RUN_ON(signaling_thread());
  const SessionDescriptionInterface* sdesc =
      (source == CS_LOCAL ? local_description() : remote_description());
  RTC_DCHECK(sdesc);

  if (ConfiguredForMedia()) {
    // Batch up operations for the worker thread.
    ScopedOperationsBatcher batcher(context_->worker_thread());

    // Note: This may perform a BlockingCall over to the network thread, which
    // we'll also do in a loop below.
    UpdatePayloadTypeDemuxingState(source, bundle_groups_by_mid, batcher);

    // Push down the new SDP media section for each audio/video transceiver.
    auto rtp_transceivers = transceivers()->ListInternal();
    std::vector<std::pair<RtpTransceiver*, const MediaContentDescription*>>
        channels;
    std::optional<RtcpFeedbackType> preferred_rtcp_cc_ack_type;
    bool all_rtp_have_same_cc_ack_type = true;
    bool any_rtp_has_cc_ack_type = false;
    std::optional<RtcpFeedbackType> first_rtp_cc_ack_type;

    for (const auto& transceiver : rtp_transceivers) {
      const ContentInfo* content_info =
          FindMediaSectionForTransceiver(transceiver, sdesc);
      if (!transceiver->HasChannel() || !content_info ||
          content_info->rejected) {
        continue;
      }
      const MediaContentDescription* content_desc =
          content_info->media_description();
      if (!content_desc) {
        continue;
      }

      if (!first_rtp_cc_ack_type.has_value()) {
        first_rtp_cc_ack_type = content_desc->preferred_rtcp_cc_ack_type();
      }

      if (content_desc->preferred_rtcp_cc_ack_type().has_value()) {
        any_rtp_has_cc_ack_type = true;
      }

      if (first_rtp_cc_ack_type != content_desc->preferred_rtcp_cc_ack_type()) {
        all_rtp_have_same_cc_ack_type = false;
      }

      transceiver->OnNegotiationUpdate(type, content_desc);
      channels.push_back(std::make_pair(transceiver, content_desc));
    }

    if (any_rtp_has_cc_ack_type && all_rtp_have_same_cc_ack_type) {
      preferred_rtcp_cc_ack_type = first_rtp_cc_ack_type;
    } else if (any_rtp_has_cc_ack_type && !all_rtp_have_same_cc_ack_type) {
      RTC_LOG(LS_ERROR) << "Warning: Inconsistent congestion control feedback "
                           "types, ignoring all.";
    }

    for (const auto& [transceiver, content] : channels) {
      if (source == CS_LOCAL) {
        transceiver->SetChannelLocalContent(content, type, batcher);
      } else {
        transceiver->SetChannelRemoteContent(content, type, batcher);
      }
    }

    RTCError error = batcher.Run();
    if (!error.ok()) {
      return error;
    }

    // If local and remote are both set, we assume that it's safe to trigger
    // CCFB.
    if (pc_->trials().IsEnabled("WebRTC-RFC8888CongestionControlFeedback")) {
      if ((type == SdpType::kAnswer || type == SdpType::kPrAnswer) &&
          local_description() && remote_description()) {
        std::optional<RtcpFeedbackType> remote_preferred_rtcp_cc_ack_type;
        bool remote_all_rtp_have_same_cc_ack_type = true;
        bool remote_any_rtp_has_cc_ack_type = false;
        std::optional<RtcpFeedbackType> remote_first_rtp_cc_ack_type;
        // Verify that the remote agrees on congestion control feedback format.
        for (const auto& content :
             remote_description()->description()->contents()) {
          if (content.type != MediaProtocolType::kRtp || content.rejected ||
              content.media_description() == nullptr) {
            continue;
          }

          if (!remote_first_rtp_cc_ack_type.has_value()) {
            remote_first_rtp_cc_ack_type =
                content.media_description()->preferred_rtcp_cc_ack_type();
          }

          if (content.media_description()
                  ->preferred_rtcp_cc_ack_type()
                  .has_value()) {
            remote_any_rtp_has_cc_ack_type = true;
          }

          if (remote_first_rtp_cc_ack_type !=
              content.media_description()->preferred_rtcp_cc_ack_type()) {
            remote_all_rtp_have_same_cc_ack_type = false;
          }
        }

        if (remote_any_rtp_has_cc_ack_type &&
            remote_all_rtp_have_same_cc_ack_type) {
          remote_preferred_rtcp_cc_ack_type = remote_first_rtp_cc_ack_type;
        } else if (remote_any_rtp_has_cc_ack_type &&
                   !remote_all_rtp_have_same_cc_ack_type) {
          RTC_LOG(LS_ERROR) << "Warning: Inconsistent remote congestion "
                               "control feedback types, ignoring all.";
        }

        if (preferred_rtcp_cc_ack_type.has_value()) {
          if (preferred_rtcp_cc_ack_type == remote_preferred_rtcp_cc_ack_type) {
            // The call and the congestion controller live on the worker thread.
            context_->worker_thread()->PostTask(
                [call = pc_->call_ptr(),
                 preferred_rtcp_cc_ack_type = *preferred_rtcp_cc_ack_type] {
                  call->SetPreferredRtcpCcAckType(preferred_rtcp_cc_ack_type);
                });
          } else {
            RTC_LOG(LS_WARNING)
                << "Inconsistent Congestion Control feedback types: "
                << preferred_rtcp_cc_ack_type << " vs "
                << remote_preferred_rtcp_cc_ack_type << " Using default.";
          }
        }
      }
    }
  }
  // Need complete offer/answer with an SCTP m= section before starting SCTP,
  // according to https://tools.ietf.org/html/draft-ietf-mmusic-sctp-sdp-19
  if (pc_->sctp_mid() && local_description() && remote_description()) {
    auto local_sctp_description =
        GetFirstSctpDataContentDescription(local_description()->description());
    auto remote_sctp_description =
        GetFirstSctpDataContentDescription(remote_description()->description());
    if (local_sctp_description && remote_sctp_description) {
      int max_message_size;
      // A remote max message size of zero means "any size supported".
      // We configure the connection with our own max message size.
      if (remote_sctp_description->max_message_size() == 0) {
        max_message_size = local_sctp_description->max_message_size();
      } else {
        max_message_size =
            std::min(local_sctp_description->max_message_size(),
                     remote_sctp_description->max_message_size());
      }
      RTCError error = pc_->StartSctpTransport(
          {.local_port = local_sctp_description->port(),
           .remote_port = remote_sctp_description->port(),
           .max_message_size = max_message_size,
           .max_sctp_streams = max_sctp_streams_,
           .local_init = local_sctp_description->sctp_init(),
           .remote_init = remote_sctp_description->sctp_init()});
      if (!error.ok()) {
        return error;
      }
    }
  }

  return RTCError::OK();
}

RTCError SdpOfferAnswerHandler::PushdownTransportDescription(
    ContentSource source,
    SdpType type) {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::PushdownTransportDescription");
  RTC_DCHECK_RUN_ON(signaling_thread());

  if (source == CS_LOCAL) {
    const SessionDescriptionInterface* sdesc = local_description();
    RTC_DCHECK(sdesc);
    // Inform our local bundle manager of the changes
    pt_suggester_.Update(sdesc->description(), /* local= */ true, type);
    const auto* remote = remote_description();
    return transport_controller_s()->SetLocalDescription(
        type, sdesc->description(), remote ? remote->description() : nullptr);
  } else {
    const SessionDescriptionInterface* sdesc = remote_description();
    RTC_DCHECK(sdesc);
    // Inform our local bundle manager of the changes
    pt_suggester_.Update(sdesc->description(), /* local= */ false, type);
    const auto* local = local_description();
    return transport_controller_s()->SetRemoteDescription(
        type, local ? local->description() : nullptr, sdesc->description());
  }
}

void SdpOfferAnswerHandler::RemoveStoppedTransceivers() {
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::RemoveStoppedTransceivers");
  RTC_DCHECK_RUN_ON(signaling_thread());
  // 3.2.10.1: For each transceiver in the connection's set of transceivers
  //           run the following steps:
  if (!IsUnifiedPlan())
    return;
  if (!ConfiguredForMedia()) {
    return;
  }
  // Traverse a copy of the transceiver list.
  auto transceiver_list = transceivers()->List();
  // Batched channel teardown for removed transceivers. The tasks run at the
  // end of this function while `transceiver_list` still holds references.
  ScopedOperationsBatcher network_teardown_tasks(context_->network_thread());
  ScopedOperationsBatcher worker_tasks(context_->worker_thread());
  for (const auto& transceiver : transceiver_list) {
    // 3.2.10.1.1: If transceiver is stopped, associated with an m= section
    //             and the associated m= section is rejected in
    //             connection.[[CurrentLocalDescription]] or
    //             connection.[[CurrentRemoteDescription]], remove the
    //             transceiver from the connection's set of transceivers.
    if (!transceiver->stopped()) {
      continue;
    }
    const ContentInfo* local_content = FindMediaSectionForTransceiver(
        transceiver->internal(), local_description());
    const ContentInfo* remote_content = FindMediaSectionForTransceiver(
        transceiver->internal(), remote_description());
    if ((local_content && local_content->rejected) ||
        (remote_content && remote_content->rejected)) {
      RTC_LOG(LS_INFO) << "Dissociating transceiver"
                          " since the media section is being recycled.";
      transceiver->internal()->set_mid(std::nullopt);
      transceiver->internal()->set_mline_index(std::nullopt);
    } else if (!local_content && !remote_content) {
      // TODO(bugs.webrtc.org/11973): Consider if this should be removed already
      // See https://github.com/w3c/webrtc-pc/issues/2576
      RTC_LOG(LS_INFO)
          << "Dropping stopped transceiver that was never associated";
    }
    // Make sure the channel is cleared before the transceiver is removed.
    if (transceiver->internal()->HasChannel()) {
      network_teardown_tasks.Add(
          transceiver->internal()->GetClearChannelNetworkTask());
      worker_tasks.Add(transceiver->internal()->GetDeleteChannelWorkerTask(
          /*stop_senders=*/false));
    }
    transceivers()->Remove(transceiver);
  }
  RTCError error = network_teardown_tasks.Run();
  RTC_DCHECK(error.ok());
  error = worker_tasks.Run();
  RTC_DCHECK(error.ok());
}

void SdpOfferAnswerHandler::RemoveUnusedChannels(
    const SessionDescription* desc) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());

  if (ConfiguredForMedia()) {
    // Destroy video channel first since it may have a pointer to the
    // voice channel.
    const ContentInfo* video_info = GetFirstVideoContent(desc);
    if (!video_info || video_info->rejected) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
      rtp_manager()->GetVideoTransceiver()->internal()->ClearChannel();
      RTC_ALLOW_PLAN_B_DEPRECATION_END();
    }

    const ContentInfo* audio_info = GetFirstAudioContent(desc);
    if (!audio_info || audio_info->rejected) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
      rtp_manager()->GetAudioTransceiver()->internal()->ClearChannel();
      RTC_ALLOW_PLAN_B_DEPRECATION_END();
    }
  }
  const ContentInfo* data_info = GetFirstDataContent(desc);
  if (!data_info) {
    RTCError error(RTCErrorType::OPERATION_ERROR_WITH_DATA,
                   "No data channel section in the description.");
    error.set_error_detail(RTCErrorDetailType::DATA_CHANNEL_FAILURE);
    pc_->DestroyDataChannelTransport(error);
  } else if (data_info->rejected) {
    StringBuilder sb;
    sb << "Rejected data channel with mid=" << data_info->mid() << ".";

    RTCError error(RTCErrorType::OPERATION_ERROR_WITH_DATA, sb.Release());
    error.set_error_detail(RTCErrorDetailType::DATA_CHANNEL_FAILURE);
    pc_->DestroyDataChannelTransport(error);
  }
}

void SdpOfferAnswerHandler::UpdateEndedRemoteMediaStreams() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<scoped_refptr<MediaStreamInterface>> streams_to_remove;
  for (size_t i = 0; i < remote_streams_->count(); ++i) {
    MediaStreamInterface* stream = remote_streams_->at(i);
    if (stream->GetAudioTracks().empty() && stream->GetVideoTracks().empty()) {
      streams_to_remove.push_back(scoped_refptr<MediaStreamInterface>(stream));
    }
  }

  pc_->RunWithObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    for (auto& stream : streams_to_remove) {
      remote_streams_->RemoveStream(stream.get());
      observer->OnRemoveStream(std::move(stream));
    }
  });
}

bool SdpOfferAnswerHandler::UseCandidatesInRemoteDescription() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  auto* remote_desc = remote_description();
  if (!remote_desc) {
    return true;
  }
  bool ret = true;

  for (size_t m = 0; m < remote_desc->number_of_mediasections(); ++m) {
    const IceCandidateCollection* candidates = remote_desc->candidates(m);
    for (size_t n = 0; n < candidates->count(); ++n) {
      const IceCandidate* candidate = candidates->at(n);
      bool valid = false;
      if (!ReadyToUseRemoteCandidate(candidate, remote_desc, &valid)) {
        if (valid) {
          RTC_LOG(LS_INFO)
              << "UseCandidatesInRemoteDescription: Not ready to use "
                 "candidate.";
        }
        continue;
      }
      ret = UseCandidate(candidate);
      if (!ret) {
        break;
      }
    }
  }
  return ret;
}

bool SdpOfferAnswerHandler::UseCandidate(const IceCandidate* candidate) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  RTCErrorOr<const ContentInfo*> result =
      FindContentInfo(remote_description(), candidate);
  if (!result.ok())
    return false;

  const Candidate& c = candidate->candidate();
  RTCError error = VerifyCandidate(c);
  if (!error.ok()) {
    RTC_LOG(LS_WARNING) << "Invalid candidate: " << c.ToString();
    return true;
  }

  pc_->AddRemoteCandidate(result.value()->mid(), c);

  return true;
}

// We need to check the local/remote description for the Transport instead of
// the session, because a new Transport added during renegotiation may have
// them unset while the session has them set from the previous negotiation.
// Not doing so may trigger the auto generation of transport description and
// mess up DTLS identity information, ICE credential, etc.
bool SdpOfferAnswerHandler::ReadyToUseRemoteCandidate(
    const IceCandidate* candidate,
    const SessionDescriptionInterface* remote_desc,
    bool* valid) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  *valid = true;

  const SessionDescriptionInterface* current_remote_desc =
      remote_desc ? remote_desc : remote_description();

  if (!current_remote_desc) {
    return false;
  }

  RTCErrorOr<const ContentInfo*> result =
      FindContentInfo(current_remote_desc, candidate);
  if (!result.ok()) {
    RTC_LOG(LS_ERROR) << "ReadyToUseRemoteCandidate: Invalid candidate. "
                      << result.error().message();

    *valid = false;
    return false;
  }

  if (has_sdp_munged_ufrag_) {
    // The format is
    // restricted_addresses:<host-with-wildcard>:<port>|<other-host>:<other-port>
    // The host can contain wildcards that match anything that comes after it.
    // The port should either be a wildcard or a specific port.
    const std::string restricted_addresses =
        pc_->trials().Lookup("WebRTC-NoSdpMangleUfragRestrictedAddresses");
    const std::string port = candidate->candidate().address().PortAsString();
    const std::string host = candidate->candidate().address().HostAsURIString();
    const std::vector<absl::string_view> restricted_address_list =
        absl::StrSplit(restricted_addresses, '|');
    bool allowed = true;
    for (const absl::string_view restricted_address : restricted_address_list) {
      const std::pair<absl::string_view, absl::string_view> address =
          absl::StrSplit(restricted_address, ':');
      if ((address.second == port || address.second == "*") &&
          WildcardHostPrefixMatch(host, address.first)) {
        RTC_LOG(LS_ERROR) << "ReadyToUseRemoteCandidate: Candidate not valid "
                             "because of SDP munging.";
        *valid = false;
        allowed = false;
        break;
      }
    }
    return allowed;
  }
  return true;
}

RTCErrorOr<const ContentInfo*> SdpOfferAnswerHandler::FindContentInfo(
    const SessionDescriptionInterface* description,
    const IceCandidate* candidate) {
  RTC_LOG_THREAD_BLOCK_COUNT();
  if (!candidate->sdp_mid().empty()) {
    auto& contents = description->description()->contents();
    auto it =
        absl::c_find_if(contents, [candidate](const ContentInfo& content_info) {
          return content_info.mid() == candidate->sdp_mid();
        });
    if (it == contents.end()) {
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::INVALID_PARAMETER)
          << "Mid " << candidate->sdp_mid()
          << " specified but no media section with that mid found.");
    } else {
      return &*it;
    }
  } else if (candidate->sdp_mline_index() >= 0) {
    size_t mediacontent_index =
        static_cast<size_t>(candidate->sdp_mline_index());
    size_t content_size = description->description()->contents().size();
    if (mediacontent_index < content_size) {
      return &description->description()->contents()[mediacontent_index];
    } else {
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::INVALID_RANGE)
          << "Media line index (" << candidate->sdp_mline_index()
          << ") out of range (number of mlines: " << content_size << ").");
    }
  }

  return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                       << "Neither sdp_mline_index nor sdp_mid specified.");
}

RTCError SdpOfferAnswerHandler::CreateChannels(const SessionDescription& desc) {
  RTC_DCHECK(!IsUnifiedPlan());
  RTC_LOG_THREAD_BLOCK_COUNT();
  TRACE_EVENT0("webrtc", "SdpOfferAnswerHandler::CreateChannels");
  // Creating the media channels. Transports should already have been created
  // at this point.
  RTC_DCHECK_RUN_ON(signaling_thread());

  ScopedOperationsBatcher worker_tasks(context_->worker_thread());
  ScopedOperationsBatcher network_tasks(network_thread());

  const ContentInfo* voice = GetFirstAudioContent(&desc);
  if (voice && !voice->rejected &&
      !rtp_manager()->GetAudioTransceiver()->internal()->HasChannel()) {
    rtp_manager()->GetAudioTransceiver()->internal()->CreateChannel(
        voice->mid(), pc_->call_ptr(), pc_->configuration()->media_config,
        pc_->SrtpRequired(), pc_->GetCryptoOptions(),
        video_bitrate_allocator_factory_.get(),
        [mid = voice->mid(), this]() {
          RTC_DCHECK_RUN_ON(network_thread());
          return transport_controller_n()->GetRtpTransport(mid);
        },
        worker_tasks, network_tasks);
  }

  const ContentInfo* video = GetFirstVideoContent(&desc);
  if (video && !video->rejected &&
      !rtp_manager()->GetVideoTransceiver()->internal()->HasChannel()) {
    rtp_manager()->GetVideoTransceiver()->internal()->CreateChannel(
        video->mid(), pc_->call_ptr(), pc_->configuration()->media_config,
        pc_->SrtpRequired(), pc_->GetCryptoOptions(),
        video_bitrate_allocator_factory_.get(),
        [mid = video->mid(), this]() {
          RTC_DCHECK_RUN_ON(network_thread());
          return transport_controller_n()->GetRtpTransport(mid);
        },
        worker_tasks, network_tasks);
  }

  const ContentInfo* data = GetFirstDataContent(&desc);
  if (data && !data->rejected &&
      !pc_->CreateDataChannelTransport(data->mid())) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to create data channel.");
  }

  RTCError error = worker_tasks.Run();
  if (!error.ok()) {
    return error;
  }
  return network_tasks.Run();
}

void SdpOfferAnswerHandler::GetMediaChannelTeardownTasks(
    ScopedOperationsBatcher& network_tasks,
    ScopedOperationsBatcher& worker_tasks) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  if (!transceivers()) {
    return;
  }
  auto list = transceivers()->List();
  for (const auto& transceiver : list) {
    if (transceiver->media_type() == MediaType::VIDEO) {
      network_tasks.Add(transceiver->internal()->GetClearChannelNetworkTask());
      worker_tasks.Add(transceiver->internal()->GetDeleteChannelWorkerTask(
          /*stop_senders=*/true));
    }
  }
  for (const auto& transceiver : list) {
    if (transceiver->media_type() == MediaType::AUDIO) {
      network_tasks.Add(transceiver->internal()->GetClearChannelNetworkTask());
      worker_tasks.Add(transceiver->internal()->GetDeleteChannelWorkerTask(
          /*stop_senders=*/true));
    }
  }
}

void SdpOfferAnswerHandler::GenerateMediaDescriptionOptions(
    const SessionDescriptionInterface* session_desc,
    RtpTransceiverDirection audio_direction,
    RtpTransceiverDirection video_direction,
    std::optional<size_t>* audio_index,
    std::optional<size_t>* video_index,
    std::optional<size_t>* data_index,
    MediaSessionOptions* session_options) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  for (const ContentInfo& content : session_desc->description()->contents()) {
    if (IsAudioContent(&content)) {
      // If we already have an audio m= section, reject this extra one.
      if (*audio_index) {
        session_options->media_description_options.push_back(
            MediaDescriptionOptions(MediaType::AUDIO, content.mid(),
                                    RtpTransceiverDirection::kInactive,
                                    /*stopped=*/true));
      } else {
        bool stopped = (audio_direction == RtpTransceiverDirection::kInactive);
        session_options->media_description_options.push_back(
            MediaDescriptionOptions(MediaType::AUDIO, content.mid(),
                                    audio_direction, stopped));
        *audio_index = session_options->media_description_options.size() - 1;
      }
      session_options->media_description_options.back().header_extensions =
          media_engine()->voice().GetRtpHeaderExtensions(&pc_->trials());
    } else if (IsVideoContent(&content)) {
      // If we already have an video m= section, reject this extra one.
      if (*video_index) {
        session_options->media_description_options.push_back(
            MediaDescriptionOptions(MediaType::VIDEO, content.mid(),
                                    RtpTransceiverDirection::kInactive,
                                    /*stopped=*/true));
      } else {
        bool stopped = (video_direction == RtpTransceiverDirection::kInactive);
        session_options->media_description_options.push_back(
            MediaDescriptionOptions(MediaType::VIDEO, content.mid(),
                                    video_direction, stopped));
        *video_index = session_options->media_description_options.size() - 1;
      }
      session_options->media_description_options.back().header_extensions =
          media_engine()->video().GetRtpHeaderExtensions(&pc_->trials());
    } else if (IsUnsupportedContent(&content)) {
      session_options->media_description_options.push_back(
          MediaDescriptionOptions(MediaType::UNSUPPORTED, content.mid(),
                                  RtpTransceiverDirection::kInactive,
                                  /*stopped=*/true));
    } else {
      RTC_DCHECK(IsDataContent(&content));
      // If we already have an data m= section, reject this extra one.
      if (*data_index) {
        session_options->media_description_options.push_back(
            GetMediaDescriptionOptionsForRejectedData(content.mid()));
      } else {
        session_options->media_description_options.push_back(
            GetMediaDescriptionOptionsForActiveData(content.mid()));
        *data_index = session_options->media_description_options.size() - 1;
      }
    }
  }
}

MediaDescriptionOptions
SdpOfferAnswerHandler::GetMediaDescriptionOptionsForActiveData(
    absl::string_view mid) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // Direction for data sections is meaningless, but legacy endpoints might
  // expect sendrecv.
  MediaDescriptionOptions options(MediaType::DATA, mid,
                                  RtpTransceiverDirection::kSendRecv,
                                  /*stopped=*/false);
  return options;
}

MediaDescriptionOptions
SdpOfferAnswerHandler::GetMediaDescriptionOptionsForRejectedData(
    absl::string_view mid) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  MediaDescriptionOptions options(MediaType::DATA, mid,
                                  RtpTransceiverDirection::kInactive,
                                  /*stopped=*/true);
  return options;
}

void SdpOfferAnswerHandler::UpdatePayloadTypeDemuxingState(
    ContentSource source,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid,
    ScopedOperationsBatcher& worker_tasks) {
  TRACE_EVENT0("webrtc",
               "SdpOfferAnswerHandler::UpdatePayloadTypeDemuxingState");
  RTC_DCHECK_RUN_ON(signaling_thread());
  // We may need to delete any created default streams and disable creation of
  // new ones on the basis of payload type. This is needed to avoid SSRC
  // collisions in Call's RtpDemuxer, in the case that a transceiver has
  // created a default stream, and then some other channel gets the SSRC
  // signaled in the corresponding Unified Plan "m=" section. Specifically, we
  // need to disable payload type based demuxing when two bundled "m=" sections
  // are using the same payload type(s). For more context
  // see https://bugs.chromium.org/p/webrtc/issues/detail?id=11477
  const SessionDescriptionInterface* sdesc =
      (source == CS_LOCAL ? local_description() : remote_description());
  struct PayloadTypes {
    std::set<int> audio_payload_types;
    std::set<int> video_payload_types;
    bool pt_demuxing_possible_audio = true;
    bool pt_demuxing_possible_video = true;
  };
  std::map<const ContentGroup*, PayloadTypes> payload_types_by_bundle;
  // If the MID is missing from *any* receiving m= section, this is set to true.
  bool mid_header_extension_missing_audio = false;
  bool mid_header_extension_missing_video = false;
  for (auto& content_info : sdesc->description()->contents()) {
    auto it = bundle_groups_by_mid.find(content_info.mid());
    const ContentGroup* bundle_group =
        it != bundle_groups_by_mid.end() ? it->second : nullptr;
    // If this m= section isn't bundled, it's safe to demux by payload type
    // since other m= sections using the same payload type will also be using
    // different transports.
    if (!bundle_group) {
      continue;
    }
    PayloadTypes* payload_types = &payload_types_by_bundle[bundle_group];
    if (content_info.rejected ||
        (source == ContentSource::CS_LOCAL &&
         !RtpTransceiverDirectionHasRecv(
             content_info.media_description()->direction())) ||
        (source == ContentSource::CS_REMOTE &&
         !RtpTransceiverDirectionHasSend(
             content_info.media_description()->direction()))) {
      // Ignore transceivers that are not receiving.
      continue;
    }
    const MediaType media_type = content_info.media_description()->type();
    if (media_type == MediaType::AUDIO || media_type == MediaType::VIDEO) {
      if (media_type == MediaType::AUDIO &&
          !mid_header_extension_missing_audio) {
        mid_header_extension_missing_audio =
            !ContentHasHeaderExtension(content_info, RtpExtension::kMidUri);
      } else if (media_type == MediaType::VIDEO &&
                 !mid_header_extension_missing_video) {
        mid_header_extension_missing_video =
            !ContentHasHeaderExtension(content_info, RtpExtension::kMidUri);
      }
      const MediaContentDescription* media_desc =
          content_info.media_description();
      for (const Codec& codec : media_desc->codecs()) {
        if (media_type == MediaType::AUDIO) {
          if (payload_types->audio_payload_types.count(codec.id)) {
            // Two m= sections are using the same payload type, thus demuxing
            // by payload type is not possible.
            if (media_type == MediaType::AUDIO) {
              payload_types->pt_demuxing_possible_audio = false;
            }
          }
          payload_types->audio_payload_types.insert(codec.id);
        } else if (media_type == MediaType::VIDEO) {
          if (payload_types->video_payload_types.count(codec.id)) {
            // Two m= sections are using the same payload type, thus demuxing
            // by payload type is not possible.
            payload_types->pt_demuxing_possible_video = false;
          }
          payload_types->video_payload_types.insert(codec.id);
        }
      }
    }
  }

  // In Unified Plan, payload type demuxing is useful for legacy endpoints that
  // don't support the MID header extension, but it can also cause incorrrect
  // forwarding of packets when going from one m= section to multiple m=
  // sections in the same BUNDLE. This only happens if media arrives prior to
  // negotiation, but this can cause missing video and unsignalled ssrc bugs
  // severe enough to warrant disabling PT demuxing in such cases. Therefore, if
  // a MID header extension is present on all m= sections for a given kind
  // (audio/video) then we use that as an OK to disable payload type demuxing in
  // BUNDLEs of that kind. However if PT demuxing was ever turned on (e.g. MID
  // was ever removed on ANY m= section of that kind) then we continue to allow
  // PT demuxing in order to prevent disabling it in follow-up O/A exchanges and
  // allowing early media by PT.
  bool bundled_pt_demux_allowed_audio = !IsUnifiedPlan() ||
                                        mid_header_extension_missing_audio ||
                                        pt_demuxing_has_been_used_audio_;
  bool bundled_pt_demux_allowed_video = !IsUnifiedPlan() ||
                                        mid_header_extension_missing_video ||
                                        pt_demuxing_has_been_used_video_;

  // Gather all updates ahead of time so that all transports can be updated in a
  // single BlockingCall; necessary due to thread guards.
  flat_map<std::string, bool> transports_to_update;
  for (RtpTransceiver* transceiver : transceivers()->ListInternal()) {
    const ContentInfo* content =
        FindMediaSectionForTransceiver(transceiver, sdesc);
    if (!content) {
      continue;
    }

    const MediaType media_type = transceiver->media_type();
    if (media_type != MediaType::AUDIO && media_type != MediaType::VIDEO) {
      continue;
    }

    RtpTransceiverDirection local_direction =
        content->media_description()->direction();
    if (source == CS_REMOTE) {
      local_direction = RtpTransceiverDirectionReversed(local_direction);
    }

    auto bundle_it = bundle_groups_by_mid.find(content->mid());
    const ContentGroup* bundle_group =
        bundle_it != bundle_groups_by_mid.end() ? bundle_it->second : nullptr;
    bool pt_demux_enabled = RtpTransceiverDirectionHasRecv(local_direction);
    if (media_type == MediaType::AUDIO) {
      pt_demux_enabled &=
          !bundle_group ||
          (bundled_pt_demux_allowed_audio &&
           payload_types_by_bundle[bundle_group].pt_demuxing_possible_audio);
      if (pt_demux_enabled) {
        pt_demuxing_has_been_used_audio_ = true;
      }
    } else {
      RTC_DCHECK_EQ(media_type, MediaType::VIDEO);
      pt_demux_enabled &=
          !bundle_group ||
          (bundled_pt_demux_allowed_video &&
           payload_types_by_bundle[bundle_group].pt_demuxing_possible_video);
      if (pt_demux_enabled) {
        pt_demuxing_has_been_used_video_ = true;
      }
    }

    // Accumulate the pt_demux_enabled state per transport (represented by mid).
    // If any transceiver associated with the transport requires PT demuxing,
    // the transport will have it enabled.
    transports_to_update[content->mid()] |= pt_demux_enabled;
  }

  if (transports_to_update.empty()) {
    return;
  }

  context_->network_thread()->BlockingCall([&]() {
    JsepTransportController* transport_controller =
        pc_->transport_controller_n();
    for (const auto& [mid, pt_demux_enabled] : transports_to_update) {
      RtpTransportInternal* rtp_transport =
          transport_controller->GetRtpTransport(mid);
      if (rtp_transport != nullptr) {
        rtp_transport->SetActivePayloadTypeDemuxing(pt_demux_enabled);
      }
    }
  });

  // If payload type demuxing is disabled for a transport, clear out any
  // unsignaled streams that might have been created previously based on
  // payload type matches. This prevents SSRC collisions if those SSRCs
  // are later reused.
  for (RtpTransceiver* transceiver : transceivers()->ListInternal()) {
    const ContentInfo* content =
        FindMediaSectionForTransceiver(transceiver, sdesc);
    if (!content) {
      continue;
    }
    auto it = transports_to_update.find(content->mid());
    if (it != transports_to_update.end() && !it->second) {
      if (MediaReceiveChannelInterface* receive_channel =
              transceiver->media_receive_channel()) {
        // TODO: bugs.webrtc.org/42221580 - This will remove *all* unsignaled
        // streams (those without an explicitly signaled SSRC), which may
        // include streams that were matched to this channel by MID or RID.
        // Ideally we'd remove only the streams that were matched based on
        // payload type alone, but currently there is no straightforward way to
        // identify those streams.
        worker_tasks.Add(receive_channel->GetResetUnsignaledRecvStreamTask());
      }
    }
  }
}

bool SdpOfferAnswerHandler::ConfiguredForMedia() const {
  return context_->is_configured_for_media();
}

}  // namespace webrtc
