/*
 *  Copyright 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/media_session.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/sctp_transport_interface.h"
#include "api/transport/sctp_transport_factory_interface.h"
#include "call/payload_type.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/base/rid_description.h"
#include "media/base/stream_params.h"
#include "p2p/base/ice_credentials_iterator.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_description_factory.h"
#include "p2p/base/transport_info.h"
#include "pc/codec_vendor.h"
#include "pc/media_options.h"
#include "pc/media_protocol_names.h"
#include "pc/rtp_media_utils.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/experiments/rfc8888_congestion_control_feedback_experiment.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "rtc_base/unique_id_generator.h"

namespace webrtc {

namespace {

RtpExtension RtpExtensionFromCapability(
    const RtpHeaderExtensionCapability& capability) {
  return RtpExtension(capability.uri,
                      capability.preferred_id.value_or(RtpHeaderExtensionId(1)),
                      capability.preferred_encrypt);
}

RtpHeaderExtensions UnstoppedRtpHeaderExtensionsFromCapabilities(
    std::span<const RtpHeaderExtensionCapability> capabilities) {
  RtpHeaderExtensions exts;
  for (const auto& capability : capabilities) {
    if (capability.direction != RtpTransceiverDirection::kStopped) {
      exts.push_back(RtpExtensionFromCapability(capability));
    }
  }
  return exts;
}

bool IsCapabilityPresent(const RtpHeaderExtensionCapability& capability,
                         const RtpHeaderExtensions& extensions) {
  return std::find_if(extensions.begin(), extensions.end(),
                      [&capability](const RtpExtension& extension) {
                        return capability.uri == extension.uri;
                      }) != extensions.end();
}

RtpHeaderExtensions UnstoppedOrPresentRtpHeaderExtensions(
    const std::vector<RtpHeaderExtensionCapability>& capabilities,
    const RtpHeaderExtensions& all_encountered_extensions) {
  RtpHeaderExtensions extensions;
  for (const auto& capability : capabilities) {
    if (capability.direction != RtpTransceiverDirection::kStopped ||
        IsCapabilityPresent(capability, all_encountered_extensions)) {
      extensions.push_back(RtpExtensionFromCapability(capability));
    }
  }
  return extensions;
}

bool ContainsRtxCodec(const std::vector<Codec>& codecs) {
  return absl::c_find_if(codecs, [](const Codec& c) {
           return c.GetResiliencyType() == Codec::ResiliencyType::kRtx;
         }) != codecs.end();
}

bool ContainsFlexfecCodec(const std::vector<Codec>& codecs) {
  return absl::c_find_if(codecs, [](const Codec& c) {
           return c.GetResiliencyType() == Codec::ResiliencyType::kFlexfec;
         }) != codecs.end();
}

bool IsComfortNoiseCodec(const Codec& codec) {
  return absl::EqualsIgnoreCase(codec.name, kComfortNoiseCodecName);
}

RtpTransceiverDirection NegotiateRtpTransceiverDirection(
    RtpTransceiverDirection offer,
    RtpTransceiverDirection wants) {
  bool offer_send = RtpTransceiverDirectionHasSend(offer);
  bool offer_recv = RtpTransceiverDirectionHasRecv(offer);
  bool wants_send = RtpTransceiverDirectionHasSend(wants);
  bool wants_recv = RtpTransceiverDirectionHasRecv(wants);
  return RtpTransceiverDirectionFromSendRecv(offer_recv && wants_send,
                                             offer_send && wants_recv);
}

bool IsMediaContentOfType(const ContentInfo* content, MediaType media_type) {
  if (!content || !content->media_description()) {
    return false;
  }
  return content->media_description()->type() == media_type;
}

// Finds all StreamParams of all media types and attach them to stream_params.
StreamParamsVec GetCurrentStreamParams(
    const std::vector<const ContentInfo*>& active_local_contents) {
  StreamParamsVec stream_params;
  for (const ContentInfo* content : active_local_contents) {
    for (const StreamParams& params : content->media_description()->streams()) {
      stream_params.push_back(params);
    }
  }
  return stream_params;
}

StreamParams CreateStreamParamsForNewSenderWithSsrcs(
    const SenderOptions& sender,
    const std::string& rtcp_cname,
    bool include_rtx_streams,
    bool include_flexfec_stream,
    UniqueRandomIdGenerator* ssrc_generator,
    const FieldTrialsView& field_trials) {
  StreamParams result;
  result.id = sender.track_id;

  // TODO(brandtr): Update when we support multistream protection.
  if (include_flexfec_stream && sender.num_sim_layers > 1) {
    include_flexfec_stream = false;
    RTC_LOG(LS_WARNING)
        << "Our FlexFEC implementation only supports protecting "
           "a single media streams. This session has multiple "
           "media streams however, so no FlexFEC SSRC will be generated.";
  }
  if (include_flexfec_stream && !field_trials.IsEnabled("WebRTC-FlexFEC-03")) {
    include_flexfec_stream = false;
    RTC_LOG(LS_WARNING)
        << "WebRTC-FlexFEC trial is not enabled, not sending FlexFEC";
  }

  result.GenerateSsrcs(sender.num_sim_layers, include_rtx_streams,
                       include_flexfec_stream, ssrc_generator);

  result.cname = rtcp_cname;
  result.set_stream_ids(sender.stream_ids);

  return result;
}

bool ValidateSimulcastLayers(const std::vector<RidDescription>& rids,
                             const SimulcastLayerList& simulcast_layers) {
  return absl::c_all_of(
      simulcast_layers.GetAllLayers(), [&rids](const SimulcastLayer& layer) {
        return absl::c_any_of(rids, [&layer](const RidDescription& rid) {
          return rid.rid == layer.rid;
        });
      });
}

StreamParams CreateStreamParamsForNewSenderWithRids(
    const SenderOptions& sender,
    const std::string& rtcp_cname) {
  RTC_DCHECK(!sender.rids.empty());
  RTC_DCHECK_EQ(sender.num_sim_layers, 0)
      << "RIDs are the compliant way to indicate simulcast.";
  RTC_DCHECK(ValidateSimulcastLayers(sender.rids, sender.simulcast_layers));
  StreamParams result;
  result.id = sender.track_id;
  result.cname = rtcp_cname;
  result.set_stream_ids(sender.stream_ids);

  // More than one rid should be signaled.
  if (sender.rids.size() > 1) {
    result.set_rids(sender.rids);
  }

  return result;
}

// Adds SimulcastDescription if indicated by the media description options.
// MediaContentDescription should already be set up with the send rids.
void AddSimulcastToMediaDescription(
    const MediaDescriptionOptions& media_description_options,
    MediaContentDescription* description) {
  RTC_DCHECK(description);

  // Check if we are using RIDs in this scenario.
  if (absl::c_all_of(description->streams(), [](const StreamParams& params) {
        return !params.has_rids();
      })) {
    return;
  }

  RTC_DCHECK_EQ(1, description->streams().size())
      << "RIDs are only supported in Unified Plan semantics.";
  RTC_DCHECK_EQ(1, media_description_options.sender_options.size());
  RTC_DCHECK(description->type() == MediaType::AUDIO ||
             description->type() == MediaType::VIDEO);

  // One RID or less indicates that simulcast is not needed.
  if (description->streams()[0].rids().size() <= 1) {
    return;
  }

  // Only negotiate the send layers.
  SimulcastDescription simulcast;
  simulcast.send_layers() =
      media_description_options.sender_options[0].simulcast_layers;
  description->set_simulcast_description(simulcast);
}

// Adds a StreamParams for each SenderOptions in `sender_options` to
// content_description.
// `current_params` - All currently known StreamParams of any media type.
bool AddStreamParams(const std::vector<SenderOptions>& sender_options,
                     const std::string& rtcp_cname,
                     UniqueRandomIdGenerator* ssrc_generator,
                     StreamParamsVec* current_streams,
                     MediaContentDescription* content_description,
                     const FieldTrialsView& field_trials) {
  // SCTP streams are not negotiated using SDP/ContentDescriptions.
  if (IsSctpProtocol(content_description->protocol())) {
    return true;
  }

  const bool include_rtx_streams =
      ContainsRtxCodec(content_description->codecs());

  const bool include_flexfec_stream =
      ContainsFlexfecCodec(content_description->codecs());

  for (const SenderOptions& sender : sender_options) {
    StreamParams* param = GetStreamByIds(*current_streams, sender.track_id);
    if (!param) {
      // This is a new sender.
      StreamParams stream_param =
          sender.rids.empty()
              ?
              // Signal SSRCs and legacy simulcast (if requested).
              CreateStreamParamsForNewSenderWithSsrcs(
                  sender, rtcp_cname, include_rtx_streams,
                  include_flexfec_stream, ssrc_generator, field_trials)
              :
              // Signal RIDs and spec-compliant simulcast (if requested).
              CreateStreamParamsForNewSenderWithRids(sender, rtcp_cname);

      content_description->AddStream(stream_param);

      // Store the new StreamParams in current_streams.
      // This is necessary so that we can use the CNAME for other media types.
      current_streams->push_back(stream_param);
    } else {
      // Use existing generated SSRCs/groups, but update the sync_label if
      // necessary. This may be needed if a MediaStreamTrack was moved from one
      // MediaStream to another.
      param->set_stream_ids(sender.stream_ids);
      content_description->AddStream(*param);
    }
  }
  return true;
}

// Updates the transport infos of the `sdesc` according to the given
// `bundle_group`. The transport infos of the content names within the
// `bundle_group` should be updated to use the ufrag, pwd and DTLS role of the
// first content within the `bundle_group`.
bool UpdateTransportInfoForBundle(const ContentGroup& bundle_group,
                                  SessionDescription* sdesc) {
  // The bundle should not be empty.
  if (!sdesc || !bundle_group.FirstContentName()) {
    return false;
  }

  // We should definitely have a transport for the first content.
  const std::string& selected_content_name = *bundle_group.FirstContentName();
  const TransportInfo* selected_transport_info =
      sdesc->GetTransportInfoByName(selected_content_name);
  if (!selected_transport_info) {
    return false;
  }

  // Set the other contents to use the same ICE credentials.
  const std::string& selected_ufrag =
      selected_transport_info->description.ice_ufrag;
  const std::string& selected_pwd =
      selected_transport_info->description.ice_pwd;
  ConnectionRole selected_connection_role =
      selected_transport_info->description.connection_role;
  for (TransportInfo& transport_info : sdesc->transport_infos()) {
    if (bundle_group.HasContentName(transport_info.content_name) &&
        transport_info.content_name != selected_content_name) {
      transport_info.description.ice_ufrag = selected_ufrag;
      transport_info.description.ice_pwd = selected_pwd;
      transport_info.description.connection_role = selected_connection_role;
    }
  }
  return true;
}

std::vector<const ContentInfo*> GetActiveContents(
    const SessionDescription& description,
    const MediaSessionOptions& session_options) {
  std::vector<const ContentInfo*> active_contents;
  for (size_t i = 0; i < description.contents().size(); ++i) {
    RTC_DCHECK_LT(i, session_options.media_description_options.size());
    const ContentInfo& content = description.contents()[i];
    const MediaDescriptionOptions& media_options =
        session_options.media_description_options[i];
    if (!content.rejected && !media_options.stopped &&
        content.mid() == media_options.mid) {
      active_contents.push_back(&content);
    }
  }
  return active_contents;
}

// Create a media content to be offered for the given `sender_options`,
// according to the given options.rtcp_mux, session_options.is_muc, codecs,
// secure_transport, crypto, and current_streams. If we don't currently have
// crypto (in current_cryptos) and it is enabled (in secure_policy), crypto is
// created (according to crypto_suites). The created content is added to the
// offer.
RTCError CreateContentOffer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const RtpHeaderExtensions& rtp_extensions,
    UniqueRandomIdGenerator* ssrc_generator,
    StreamParamsVec* current_streams,
    MediaContentDescription* offer,
    const FieldTrialsView& field_trials) {
  offer->set_rtcp_mux(session_options.rtcp_mux_enabled);
  offer->set_rtcp_reduced_size(true);
  offer->set_receive_non_sender_rtt(
      !field_trials.IsDisabled("WebRTC-RtcpXrReceiverReferenceTime"));

  // Build the vector of header extensions with directions for this
  // media_description's options.
  RtpHeaderExtensions extensions;
  for (const auto& extension_with_id : rtp_extensions) {
    for (const auto& extension : media_description_options.header_extensions) {
      if (extension_with_id.uri == extension.uri &&
          extension_with_id.encrypt == extension.preferred_encrypt) {
        // TODO(crbug.com/1051821): Configure the extension direction from
        // the information in the media_description_options extension
        // capability.
        if (extension.direction != RtpTransceiverDirection::kStopped) {
          extensions.push_back(extension_with_id);
        }
      }
    }
  }
  offer->set_rtp_header_extensions(extensions);

  AddSimulcastToMediaDescription(media_description_options, offer);

  return RTCError::OK();
}

RTCError CreateMediaContentOffer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const std::vector<Codec>& codecs,
    const RtpHeaderExtensions& rtp_extensions,
    UniqueRandomIdGenerator* ssrc_generator,
    StreamParamsVec* current_streams,
    MediaContentDescription* offer,
    const FieldTrialsView& field_trials) {
  offer->AddCodecs(codecs);
  if (!AddStreamParams(media_description_options.sender_options,
                       session_options.rtcp_cname, ssrc_generator,
                       current_streams, offer, field_trials)) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to add stream parameters");
  }

  return CreateContentOffer(media_description_options, session_options,
                            rtp_extensions, ssrc_generator, current_streams,
                            offer, field_trials);
}

// Adds all extensions from `reference_extensions` to `offered_extensions` that
// don't already exist in `offered_extensions` and ensures the IDs don't
// collide. If an extension is added, it's also added to
// `all_encountered_extensions`. Also when doing the addition a new ID is set
// for that extension. `offered_extensions` is for either audio or video while
// `all_encountered_extensions` is used for both audio and video. There could be
// overlap between audio extensions and video extensions.
void MergeRtpHdrExts(const RtpHeaderExtensions& reference_extensions,
                     bool enable_encrypted_rtp_header_extensions,
                     RtpHeaderExtensions* offered_extensions,
                     RtpHeaderExtensions* all_encountered_extensions,
                     PayloadTypeSuggester& suggester,
                     absl::string_view mid,
                     RtpTransceiverIdDomain id_domain) {
  for (const auto& reference_extension : reference_extensions) {
    if (!RtpExtension::FindHeaderExtensionByUriAndEncryption(
            *offered_extensions, reference_extension.uri,
            reference_extension.encrypt)) {
      if (reference_extension.encrypt &&
          !enable_encrypted_rtp_header_extensions) {
        // Negotiating of encrypted headers is deactivated.
        continue;
      }
      const RtpExtension* existing =
          RtpExtension::FindHeaderExtensionByUriAndEncryption(
              *all_encountered_extensions, reference_extension.uri,
              reference_extension.encrypt);
      if (existing) {
        // E.g. in the case where the same RTP header extension is used for
        // audio and video.
        offered_extensions->push_back(*existing);
      } else {
        auto suggested_id = suggester.SuggestRtpHeaderExtensionId(
            mid, reference_extension, id_domain);
        RtpExtension extension = reference_extension;
        if (suggested_id.ok()) {
          extension.id = suggested_id.value();
        } else {
          RTC_LOG(LS_ERROR)
              << "Failed to suggest RTP header extension ID for "
              << reference_extension.uri << ", error " << suggested_id.error();
        }
        all_encountered_extensions->push_back(extension);
        offered_extensions->push_back(std::move(extension));
      }
    }
  }
}

// Mostly identical to RtpExtension::FindHeaderExtensionByUri but discards any
// encrypted extensions that this implementation cannot encrypt.
const RtpExtension* FindHeaderExtensionByUriDiscardUnsupported(
    const std::vector<RtpExtension>& extensions,
    absl::string_view uri,
    RtpExtension::Filter filter) {
  // Note: While it's technically possible to decrypt extensions that we don't
  // encrypt, the symmetric API of libsrtp does not allow us to supply
  // different IDs for encryption/decryption of header extensions depending on
  // whether the packet is inbound or outbound. Thereby, we are limited to
  // what we can send in encrypted form.
  if (!RtpExtension::IsEncryptionSupported(uri)) {
    // If there's no encryption support and we only want encrypted extensions,
    // there's no point in continuing the search here.
    if (filter == RtpExtension::kRequireEncryptedExtension) {
      return nullptr;
    }

    // Instruct to only return non-encrypted extensions
    filter = RtpExtension::Filter::kDiscardEncryptedExtension;
  }

  return RtpExtension::FindHeaderExtensionByUri(extensions, uri, filter);
}

void NegotiateRtpHeaderExtensions(const RtpHeaderExtensions& local_extensions,
                                  const RtpHeaderExtensions& offered_extensions,
                                  RtpExtension::Filter filter,
                                  RtpHeaderExtensions* negotiated_extensions,
                                  PayloadTypeSuggester& suggester,
                                  absl::string_view mid,
                                  RtpTransceiverIdDomain id_domain) {
  bool frame_descriptor_in_local = false;
  bool dependency_descriptor_in_local = false;
  bool abs_capture_time_in_local = false;

  auto negotiate_extension = [&](const RtpExtension& ours,
                                 const RtpExtension* theirs) {
    if (theirs && theirs->encrypt == ours.encrypt) {
      // We MUST use their ID in the answer, and we should also record it
      // in our suggester to ensure consistency and persistence.
      RTCError error =
          suggester.AddRtpHeaderExtensionMapping(mid, *theirs, /*local=*/false);
      RTC_DCHECK(error.ok());
      negotiated_extensions->push_back(*theirs);
    }
  };

  for (const RtpExtension& ours : local_extensions) {
    if (ours.uri == RtpExtension::kGenericFrameDescriptorUri00)
      frame_descriptor_in_local = true;
    else if (ours.uri == RtpExtension::kDependencyDescriptorUri)
      dependency_descriptor_in_local = true;
    else if (ours.uri == RtpExtension::kAbsoluteCaptureTimeUri)
      abs_capture_time_in_local = true;

    const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(
        offered_extensions, ours.uri, filter);
    negotiate_extension(ours, theirs);
  }

  // Frame descriptors support. If the extension is not present locally, but is
  // in the offer, we add it to the list.
  if (!dependency_descriptor_in_local) {
    const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(
        offered_extensions, RtpExtension::kDependencyDescriptorUri, filter);
    if (theirs) {
      RTCError error =
          suggester.AddRtpHeaderExtensionMapping(mid, *theirs, /*local=*/false);
      RTC_DCHECK(error.ok());
      negotiated_extensions->push_back(*theirs);
    }
  }
  if (!frame_descriptor_in_local) {
    const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(
        offered_extensions, RtpExtension::kGenericFrameDescriptorUri00, filter);
    if (theirs) {
      RTCError error =
          suggester.AddRtpHeaderExtensionMapping(mid, *theirs, /*local=*/false);
      RTC_DCHECK(error.ok());
      negotiated_extensions->push_back(*theirs);
    }
  }

  // Absolute capture time support. If the extension is not present locally, but
  // is in the offer, we add it to the list.
  if (!abs_capture_time_in_local) {
    const RtpExtension* theirs = FindHeaderExtensionByUriDiscardUnsupported(
        offered_extensions, RtpExtension::kAbsoluteCaptureTimeUri, filter);
    if (theirs) {
      RTCError error =
          suggester.AddRtpHeaderExtensionMapping(mid, *theirs, /*local=*/false);
      RTC_DCHECK(error.ok());
      negotiated_extensions->push_back(*theirs);
    }
  }
}

bool SetCodecsInAnswer(const MediaContentDescription* offer,
                       const std::vector<Codec>& local_codecs,
                       const MediaDescriptionOptions& media_description_options,
                       const MediaSessionOptions& session_options,
                       UniqueRandomIdGenerator* ssrc_generator,
                       StreamParamsVec* current_streams,
                       MediaContentDescription* answer,
                       const FieldTrialsView& field_trials) {
  RTC_DCHECK(offer->type() == MediaType::AUDIO ||
             offer->type() == MediaType::VIDEO);
  answer->AddCodecs(local_codecs);
  answer->set_protocol(offer->protocol());
  if (!AddStreamParams(media_description_options.sender_options,
                       session_options.rtcp_cname, ssrc_generator,
                       current_streams, answer, field_trials)) {
    return false;  // Something went seriously wrong.
  }
  return true;
}

// Negotiates Sframe support between the offer and the local answerer options.
bool NegotiateSframeUsage(
    const MediaContentDescription* offer,
    const MediaDescriptionOptions& media_description_options) {
  return offer->sframe_enabled() && media_description_options.sframe_enabled;
}

// Create a media content to be answered for the given `sender_options`
// according to the given session_options.rtcp_mux, session_options.streams,
// codecs, crypto, and current_streams.  If we don't currently have crypto (in
// current_cryptos) and it is enabled (in secure_policy), crypto is created
// (according to crypto_suites). The codecs, rtcp_mux, and crypto are all
// negotiated with the offer. If the negotiation fails, this method returns
// false.  The created content is added to the offer.
bool CreateMediaContentAnswer(
    const MediaContentDescription* offer,
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const RtpHeaderExtensions& local_rtp_extensions,
    UniqueRandomIdGenerator* ssrc_generator,
    bool enable_encrypted_rtp_header_extensions,
    StreamParamsVec* current_streams,
    bool bundle_enabled,
    MediaContentDescription* answer,
    PayloadTypeSuggester& suggester,
    RtpTransceiverIdDomain id_domain,
    const FieldTrialsView& field_trials) {
  answer->set_extmap_allow_mixed_level(offer->extmap_allow_mixed_level());
  const RtpExtension::Filter extensions_filter =
      enable_encrypted_rtp_header_extensions
          ? RtpExtension::Filter::kPreferEncryptedExtension
          : RtpExtension::Filter::kDiscardEncryptedExtension;

  // Filter local extensions by capabilities and direction.
  RtpHeaderExtensions local_rtp_extensions_to_reply_with;
  for (const auto& extension_with_id : local_rtp_extensions) {
    for (const auto& extension : media_description_options.header_extensions) {
      if (extension_with_id.uri == extension.uri &&
          extension_with_id.encrypt == extension.preferred_encrypt) {
        // TODO(crbug.com/1051821): Configure the extension direction from
        // the information in the media_description_options extension
        // capability. For now, do not include stopped extensions.
        // See also crbug.com/webrtc/7477 about the general lack of direction.
        if (extension.direction != RtpTransceiverDirection::kStopped) {
          local_rtp_extensions_to_reply_with.push_back(extension_with_id);
          break;
        }
      }
    }
  }
  RtpHeaderExtensions negotiated_rtp_extensions;
  NegotiateRtpHeaderExtensions(
      local_rtp_extensions_to_reply_with, offer->rtp_header_extensions(),
      extensions_filter, &negotiated_rtp_extensions, suggester,
      media_description_options.mid, id_domain);
  answer->set_rtp_header_extensions(negotiated_rtp_extensions);
  // Cryptex is declarative, i.e. does not depend on the offer.
  // If present in the offer we match the level (session/media)
  // and use media level otherwise.
  if (session_options.crypto_options.srtp.cryptex_policy !=
      CryptoOptions::Srtp::CryptexPolicy::kDisabled) {
    answer->set_cryptex_level(
        offer->cryptex_level() != MediaContentDescription::AttributeLevel::kNone
            ? offer->cryptex_level()
            : MediaContentDescription::AttributeLevel::kMedia);
  }

  answer->set_rtcp_mux(session_options.rtcp_mux_enabled && offer->rtcp_mux());
  answer->set_rtcp_reduced_size(offer->rtcp_reduced_size());
  answer->set_receive_non_sender_rtt(
      offer->receive_non_sender_rtt() &&
      !field_trials.IsDisabled("WebRTC-RtcpXrReceiverReferenceTime"));
  answer->set_remote_estimate(offer->remote_estimate());

  AddSimulcastToMediaDescription(media_description_options, answer);

  answer->set_direction(NegotiateRtpTransceiverDirection(
      offer->direction(), media_description_options.direction));
  answer->set_sframe_enabled(
      NegotiateSframeUsage(offer, media_description_options));

  return true;
}

bool IsMediaProtocolSupported(MediaType type,
                              absl::string_view protocol,
                              bool secure_transport) {
  // Since not all applications serialize and deserialize the media protocol,
  // we will have to accept `protocol` to be empty.
  if (protocol.empty()) {
    return true;
  }

  if (type == MediaType::DATA) {
    // Check for SCTP
    if (secure_transport) {
      // Most likely scenarios first.
      return IsDtlsSctp(protocol);
    } else {
      return IsPlainSctp(protocol);
    }
  }

  // Allow for non-DTLS RTP protocol even when using DTLS because that's what
  // JSEP specifies.
  if (secure_transport) {
    // Most likely scenarios first.
    return IsDtlsRtp(protocol) || IsPlainRtp(protocol);
  } else {
    return IsPlainRtp(protocol);
  }
}

void SetMediaProtocol(bool secure_transport, MediaContentDescription* desc) {
  if (secure_transport)
    desc->set_protocol(kMediaProtocolDtlsSavpf);
  else
    desc->set_protocol(kMediaProtocolAvpf);
}

// Gets the TransportInfo of the given `content_name` from the
// `current_description`. If doesn't exist, returns a new one.
const TransportDescription* GetTransportDescription(
    const std::string& content_name,
    const SessionDescription* current_description) {
  const TransportDescription* desc = nullptr;
  if (current_description) {
    const TransportInfo* info =
        current_description->GetTransportInfoByName(content_name);
    if (info) {
      desc = &info->description;
    }
  }
  return desc;
}

bool OfferRfc8888(const FieldTrialsView& field_trials) {
  if (!IsRfc8888CongestionControlFeedbackEnabled(field_trials)) {
    return false;
  }
  FieldTrialParameter<bool> offer_rfc_8888("offer", false);
  ParseFieldTrial(
      {&offer_rfc_8888},
      field_trials.Lookup(kRfc8888CongestionControlFeedbackFieldTrial));
  return offer_rfc_8888;
}

bool AcceptOfferWithRfc8888(const FieldTrialsView& field_trials) {
  return IsRfc8888CongestionControlFeedbackEnabled(field_trials);
}

}  // namespace

MediaSessionDescriptionFactory::MediaSessionDescriptionFactory(
    const Environment& env,
    UniqueRandomIdGenerator* ssrc_generator,
    const TransportDescriptionFactory* transport_desc_factory,
    SctpTransportFactoryInterface* sctp_factory,
    CodecLookupHelper* codec_lookup_helper)
    : offer_rfc_8888_(OfferRfc8888(transport_desc_factory->trials())),
      accept_offer_with_rfc_8888_(
          AcceptOfferWithRfc8888(transport_desc_factory->trials())),
      ssrc_generator_(ssrc_generator),
      transport_desc_factory_(transport_desc_factory),
      sctp_factory_(sctp_factory),
      codec_lookup_helper_(codec_lookup_helper),
      payload_types_in_transport_trial_enabled_(
          env.field_trials().IsEnabled("WebRTC-PayloadTypesInTransport")),
      env_(env) {
  RTC_CHECK(transport_desc_factory_);
  RTC_CHECK(codec_lookup_helper_);
}

RtpHeaderExtensions
MediaSessionDescriptionFactory::filtered_rtp_header_extensions(
    RtpHeaderExtensions extensions) const {
  if (!is_unified_plan_) {
    // Remove extensions only supported with unified-plan.
    std::erase_if(extensions, [](const RtpExtension& extension) {
      return extension.uri == RtpExtension::kMidUri ||
             extension.uri == RtpExtension::kRidUri ||
             extension.uri == RtpExtension::kRepairedRidUri;
    });
  }
  return extensions;
}

RTCErrorOr<std::unique_ptr<SessionDescription>>
MediaSessionDescriptionFactory::CreateOfferOrError(
    const MediaSessionOptions& session_options,
    const SessionDescription* current_description) const {
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  // Must have options for each existing section.
  if (current_description) {
    RTC_DCHECK_LE(current_description->contents().size(),
                  session_options.media_description_options.size());
  }

  IceCredentialsIterator ice_credentials(
      session_options.pooled_ice_credentials);

  std::vector<const ContentInfo*> current_active_contents;
  if (current_description) {
    current_active_contents =
        GetActiveContents(*current_description, session_options);
  }

  StreamParamsVec current_streams =
      GetCurrentStreamParams(current_active_contents);

  AudioVideoRtpHeaderExtensions extensions_with_ids =
      GetOfferedRtpHeaderExtensionsWithIds(
          current_active_contents, session_options.offer_extmap_allow_mixed,
          session_options.media_description_options);

  auto offer = std::make_unique<SessionDescription>();

  // Iterate through the media description options, matching with existing media
  // descriptions in `current_description`.
  size_t msection_index = 0;
  for (const MediaDescriptionOptions& media_description_options :
       session_options.media_description_options) {
    const ContentInfo* current_content = nullptr;
    if (current_description &&
        msection_index < current_description->contents().size()) {
      current_content = &current_description->contents()[msection_index];
      // Media type must match unless this media section is being recycled.
    }
    RTCError error;
    switch (media_description_options.type) {
      case MediaType::AUDIO:
      case MediaType::VIDEO:
        error = AddRtpContentForOffer(
            media_description_options, session_options, current_content,
            current_description,
            media_description_options.type == MediaType::AUDIO
                ? extensions_with_ids.audio
                : extensions_with_ids.video,
            &current_streams, offer.get(), &ice_credentials);
        break;
      case MediaType::DATA:
        error = AddDataContentForOffer(media_description_options,
                                       session_options, current_content,
                                       current_description, &current_streams,
                                       offer.get(), &ice_credentials);
        break;
      case MediaType::UNSUPPORTED:
        error = AddUnsupportedContentForOffer(
            media_description_options, session_options, current_content,
            current_description, offer.get(), &ice_credentials);
        break;
      default:
        RTC_DCHECK_NOTREACHED();
    }
    if (!error.ok()) {
      return error;
    }
    ++msection_index;
  }

  // Bundle the contents together, if we've been asked to do so, and update any
  // parameters that need to be tweaked for BUNDLE.
  if (session_options.bundle_enabled) {
    ContentGroup offer_bundle(GROUP_TYPE_BUNDLE);
    for (const ContentInfo& content : offer->contents()) {
      if (content.rejected) {
        continue;
      }
      // TODO(deadbeef): There are conditions that make bundling two media
      // descriptions together illegal. For example, they use the same payload
      // type to represent different codecs, or same IDs for different header
      // extensions. We need to detect this and not try to bundle those media
      // descriptions together.
      offer_bundle.AddContentName(content.mid());
    }
    if (!offer_bundle.content_names().empty()) {
      offer->AddGroup(offer_bundle);
      if (!UpdateTransportInfoForBundle(offer_bundle, offer.get())) {
        return RTC_LOG_ERROR(
            RTCError(RTCErrorType::INTERNAL_ERROR)
            << "CreateOffer failed to UpdateTransportInfoForBundle");
      }
    }
  }

  // The following determines how to signal MSIDs to ensure compatibility with
  // older endpoints (in particular, older Plan B endpoints).
  if (is_unified_plan_) {
    // Be conservative and signal using both a=msid and a=ssrc lines. Unified
    // Plan answerers will look at a=msid and Plan B answerers will look at the
    // a=ssrc MSID line.
    offer->set_msid_signaling(kMsidSignalingSemantic |
                              kMsidSignalingMediaSection |
                              kMsidSignalingSsrcAttribute);
  } else {
    // Plan B always signals MSID using a=ssrc lines.
    offer->set_msid_signaling(kMsidSignalingSemantic |
                              kMsidSignalingSsrcAttribute);
  }

  offer->set_extmap_allow_mixed(session_options.offer_extmap_allow_mixed);
  offer->set_cryptex(session_options.crypto_options.srtp.cryptex_policy !=
                     CryptoOptions::Srtp::CryptexPolicy::kDisabled);

  return offer;
}

RTCErrorOr<std::unique_ptr<SessionDescription>>
MediaSessionDescriptionFactory::CreateAnswerOrError(
    const SessionDescription* offer,
    const MediaSessionOptions& session_options,
    const SessionDescription* current_description) const {
  if (!offer) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Called without offer.");
  }

  // Must have options for exactly as many sections as in the offer.
  RTC_DCHECK_EQ(offer->contents().size(),
                session_options.media_description_options.size());

  IceCredentialsIterator ice_credentials(
      session_options.pooled_ice_credentials);

  std::vector<const ContentInfo*> current_active_contents;
  if (current_description) {
    current_active_contents =
        GetActiveContents(*current_description, session_options);
  }

  StreamParamsVec current_streams =
      GetCurrentStreamParams(current_active_contents);

  // Decide what congestion control feedback format we're using.
  bool has_ack_ccfb = false;
  if (accept_offer_with_rfc_8888_) {
    bool all_rtp_have_ccfb = true;
    bool any_rtp_has_ccfb = false;
    for (const auto& content : offer->contents()) {
      if (content.type != MediaProtocolType::kRtp) {
        continue;
      }
      if (content.media_description()->rtcp_fb_ack_ccfb()) {
        any_rtp_has_ccfb = true;
      } else {
        all_rtp_have_ccfb = false;
      }
    }
    if (any_rtp_has_ccfb) {
      if (all_rtp_have_ccfb) {
        has_ack_ccfb = true;
      } else {
        RTC_LOG(LS_ERROR)
            << "Inconsistent rtcp_fb_ack_ccfb marking, ignoring all";
      }
    }
  }

  auto answer = std::make_unique<SessionDescription>();

  // If the offer supports BUNDLE, and we want to use it too, create a BUNDLE
  // group in the answer with the appropriate content names.
  std::vector<const ContentGroup*> offer_bundles =
      offer->GetGroupsByName(GROUP_TYPE_BUNDLE);
  // There are as many answer BUNDLE groups as offer BUNDLE groups (even if
  // rejected, we respond with an empty group). `offer_bundles`,
  // `answer_bundles` and `bundle_transports` share the same size and indices.
  std::vector<ContentGroup> answer_bundles;
  std::vector<std::unique_ptr<TransportInfo>> bundle_transports;
  answer_bundles.reserve(offer_bundles.size());
  bundle_transports.reserve(offer_bundles.size());
  for (size_t i = 0; i < offer_bundles.size(); ++i) {
    answer_bundles.emplace_back(GROUP_TYPE_BUNDLE);
    bundle_transports.emplace_back(nullptr);
  }

  answer->set_extmap_allow_mixed(offer->extmap_allow_mixed());
  // Cryptex is declarative: advertise support when policy allows. If the
  // offer used cryptex at media level, answer at media level.
  bool use_session_level_cryptex =
      offer->cryptex() ||
      absl::c_find_if(offer->contents(), [](const ContentInfo& content) {
        return content.media_description()->cryptex_level() ==
               MediaContentDescription::AttributeLevel::kMedia;
      }) == offer->contents().end();
  answer->set_cryptex(session_options.crypto_options.srtp.cryptex_policy !=
                          CryptoOptions::Srtp::CryptexPolicy::kDisabled &&
                      use_session_level_cryptex);

  // Iterate through the media description options, matching with existing
  // media descriptions in `current_description`.
  size_t msection_index = 0;
  for (const MediaDescriptionOptions& media_description_options :
       session_options.media_description_options) {
    const ContentInfo* offer_content = &offer->contents()[msection_index];
    // Media types and MIDs must match between the remote offer and the
    // MediaDescriptionOptions.
    RTC_DCHECK(
        IsMediaContentOfType(offer_content, media_description_options.type));
    RTC_DCHECK(media_description_options.mid == offer_content->mid());
    // Get the index of the BUNDLE group that this MID belongs to, if any.
    std::optional<size_t> bundle_index;
    for (size_t i = 0; i < offer_bundles.size(); ++i) {
      if (offer_bundles[i]->HasContentName(media_description_options.mid)) {
        bundle_index = i;
        break;
      }
    }
    TransportInfo* bundle_transport =
        bundle_index.has_value() ? bundle_transports[bundle_index.value()].get()
                                 : nullptr;

    const ContentInfo* current_content = nullptr;
    if (current_description &&
        msection_index < current_description->contents().size()) {
      current_content = &current_description->contents()[msection_index];
    }
    // Don't offer the transport-cc header extension if "ack ccfb" is in use.
    RtpHeaderExtensions header_extensions;
    if (has_ack_ccfb) {
      auto header_extensions_in = media_description_options.header_extensions;
      for (auto& option : header_extensions_in) {
        if (option.uri == RtpExtension::kTransportSequenceNumberUri) {
          option.direction = RtpTransceiverDirection::kStopped;
        }
      }
      header_extensions =
          UnstoppedRtpHeaderExtensionsFromCapabilities(header_extensions_in);
    } else {
      header_extensions = UnstoppedRtpHeaderExtensionsFromCapabilities(
          media_description_options.header_extensions);
    }
    RTCError error;
    switch (media_description_options.type) {
      case MediaType::AUDIO:
      case MediaType::VIDEO:
        error = AddRtpContentForAnswer(
            media_description_options, session_options, offer_content, offer,
            current_content, current_description, bundle_transport,
            header_extensions, &current_streams, answer.get(), has_ack_ccfb,
            &ice_credentials);
        break;
      case MediaType::DATA:
        error = AddDataContentForAnswer(
            media_description_options, session_options, offer_content, offer,
            current_content, current_description, bundle_transport,
            &current_streams, answer.get(), &ice_credentials);
        break;
      case MediaType::UNSUPPORTED:
        error = AddUnsupportedContentForAnswer(
            media_description_options, session_options, offer_content, offer,
            current_content, current_description, bundle_transport,
            answer.get(), &ice_credentials);
        break;
      default:
        RTC_DCHECK_NOTREACHED();
    }
    if (!error.ok()) {
      return error;
    }
    ++msection_index;
    // See if we can add the newly generated m= section to the BUNDLE group in
    // the answer.
    ContentInfo& added = answer->contents().back();
    if (!added.rejected && session_options.bundle_enabled &&
        bundle_index.has_value()) {
      // The `bundle_index` is for `media_description_options.mid`.
      RTC_DCHECK_EQ(media_description_options.mid, added.mid());
      answer_bundles[bundle_index.value()].AddContentName(added.mid());
      bundle_transports[bundle_index.value()].reset(
          new TransportInfo(*answer->GetTransportInfoByName(added.mid())));
    }
  }

  // If BUNDLE group(s) were offered, put the same number of BUNDLE groups in
  // the answer even if they're empty. RFC5888 says:
  //
  //   A SIP entity that receives an offer that contains an "a=group" line
  //   with semantics that are understood MUST return an answer that
  //   contains an "a=group" line with the same semantics.
  if (!offer_bundles.empty()) {
    for (const ContentGroup& answer_bundle : answer_bundles) {
      answer->AddGroup(answer_bundle);

      if (answer_bundle.FirstContentName()) {
        // Share the same ICE credentials and crypto params across all contents,
        // as BUNDLE requires.
        if (!UpdateTransportInfoForBundle(answer_bundle, answer.get())) {
          return RTC_LOG_ERROR(
              RTCError(RTCErrorType::INTERNAL_ERROR)
              << "CreateAnswer failed to UpdateTransportInfoForBundle.");
        }
      }
    }
  }

  // The following determines how to signal MSIDs to ensure compatibility with
  // older endpoints (in particular, older Plan B endpoints).
  if (is_unified_plan_) {
    // Unified Plan needs to look at what the offer included to find the most
    // compatible answer.
    int msid_signaling = offer->msid_signaling();
    if (msid_signaling == (kMsidSignalingSemantic | kMsidSignalingMediaSection |
                           kMsidSignalingSsrcAttribute)) {
      // If both a=msid and a=ssrc MSID signaling methods were used, we're
      // probably talking to a Unified Plan endpoint so respond with just
      // a=msid.
      answer->set_msid_signaling(kMsidSignalingSemantic |
                                 kMsidSignalingMediaSection);
    } else if (msid_signaling ==
                   (kMsidSignalingSemantic | kMsidSignalingSsrcAttribute) ||
               msid_signaling == kMsidSignalingSsrcAttribute) {
      // If only a=ssrc MSID signaling method was used, we're probably talking
      // to a Plan B endpoint so respond with just a=ssrc MSID.
      answer->set_msid_signaling(kMsidSignalingSemantic |
                                 kMsidSignalingSsrcAttribute);
    } else {
      // We end up here in one of three cases:
      // 1. An empty offer. We'll reply with an empty answer so it doesn't
      //    matter what we pick here.
      // 2. A data channel only offer. We won't add any MSIDs to the answer so
      //    it also doesn't matter what we pick here.
      // 3. Media that's either recvonly or inactive from the remote point of
      // view.
      //    We don't have any information to say whether the endpoint is Plan B
      //    or Unified Plan. Since plan-b is obsolete, do not respond with it.
      //    We assume that endpoints not supporting MSID will silently ignore
      //    the a=msid lines they do not understand.
      answer->set_msid_signaling(kMsidSignalingSemantic |
                                 kMsidSignalingMediaSection);
    }
  } else {
    // Plan B always signals MSID using a=ssrc lines.
    answer->set_msid_signaling(kMsidSignalingSemantic |
                               kMsidSignalingSsrcAttribute);
  }

  return answer;
}

MediaSessionDescriptionFactory::AudioVideoRtpHeaderExtensions
MediaSessionDescriptionFactory::GetOfferedRtpHeaderExtensionsWithIds(
    const std::vector<const ContentInfo*>& current_active_contents,
    bool extmap_allow_mixed,
    const std::vector<MediaDescriptionOptions>& media_description_options)
    const {
  // All header extensions allocated from the same range to avoid potential
  // issues when using BUNDLE.

  // Strictly speaking the SDP attribute extmap_allow_mixed signals that the
  // receiver supports an RTP stream where one- and two-byte RTP header
  // extensions are mixed. For backwards compatibility reasons it's used in
  // WebRTC to signal that two-byte RTP header extensions are supported.
  RtpTransceiverIdDomain id_domain =
      extmap_allow_mixed ? RtpTransceiverIdDomain::kTwoByteAllowed
                         : RtpTransceiverIdDomain::kOneByteOnly;

  RtpHeaderExtensions all_encountered_extensions;

  AudioVideoRtpHeaderExtensions offered_extensions;
  // First - get all extensions from the current description if the media type
  // is used.
  // Add them to the suggester so the local ids are not reused if a new media
  // type is added.
  RTC_DCHECK(codec_lookup_helper_->PayloadTypeSuggester());
  for (const ContentInfo* content : current_active_contents) {
    if (IsMediaContentOfType(content, MediaType::AUDIO)) {
      MergeRtpHdrExts(content->media_description()->rtp_header_extensions(),
                      enable_encrypted_rtp_header_extensions_,
                      &offered_extensions.audio, &all_encountered_extensions,
                      *codec_lookup_helper_->PayloadTypeSuggester(),
                      content->mid(), id_domain);
    } else if (IsMediaContentOfType(content, MediaType::VIDEO)) {
      MergeRtpHdrExts(content->media_description()->rtp_header_extensions(),
                      enable_encrypted_rtp_header_extensions_,
                      &offered_extensions.video, &all_encountered_extensions,
                      *codec_lookup_helper_->PayloadTypeSuggester(),
                      content->mid(), id_domain);
    }
  }

  // Add all encountered header extensions in the media description options that
  // are not in the current description.
  RTC_DCHECK(codec_lookup_helper_->PayloadTypeSuggester());
  for (const auto& entry : media_description_options) {
    RtpHeaderExtensions filtered_extensions =
        filtered_rtp_header_extensions(UnstoppedOrPresentRtpHeaderExtensions(
            entry.header_extensions, all_encountered_extensions));
    if (entry.type == MediaType::AUDIO)
      MergeRtpHdrExts(
          filtered_extensions, enable_encrypted_rtp_header_extensions_,
          &offered_extensions.audio, &all_encountered_extensions,
          *codec_lookup_helper_->PayloadTypeSuggester(), entry.mid, id_domain);
    else if (entry.type == MediaType::VIDEO)
      MergeRtpHdrExts(
          filtered_extensions, enable_encrypted_rtp_header_extensions_,
          &offered_extensions.video, &all_encountered_extensions,
          *codec_lookup_helper_->PayloadTypeSuggester(), entry.mid, id_domain);
  }
  return offered_extensions;
}

RTCError MediaSessionDescriptionFactory::AddTransportOffer(
    const std::string& content_name,
    const TransportOptions& transport_options,
    const SessionDescription* current_desc,
    SessionDescription* offer_desc,
    IceCredentialsIterator* ice_credentials) const {
  const TransportDescription* current_tdesc =
      GetTransportDescription(content_name, current_desc);
  std::unique_ptr<TransportDescription> new_tdesc(
      transport_desc_factory_->CreateOffer(transport_options, current_tdesc,
                                           ice_credentials));
  if (!new_tdesc) {
    RTC_LOG(LS_ERROR) << "Failed to AddTransportOffer, content name="
                      << content_name;
  }
  offer_desc->AddTransportInfo(TransportInfo(content_name, *new_tdesc));
  return RTCError::OK();
}

std::unique_ptr<TransportDescription>
MediaSessionDescriptionFactory::CreateTransportAnswer(
    const std::string& content_name,
    const SessionDescription* offer_desc,
    const TransportOptions& transport_options,
    const SessionDescription* current_desc,
    bool require_transport_attributes,
    IceCredentialsIterator* ice_credentials) const {
  const TransportDescription* offer_tdesc =
      GetTransportDescription(content_name, offer_desc);
  const TransportDescription* current_tdesc =
      GetTransportDescription(content_name, current_desc);
  return transport_desc_factory_->CreateAnswer(offer_tdesc, transport_options,
                                               require_transport_attributes,
                                               current_tdesc, ice_credentials);
}

RTCError MediaSessionDescriptionFactory::AddTransportAnswer(
    const std::string& content_name,
    const TransportDescription& transport_desc,
    SessionDescription* answer_desc) const {
  answer_desc->AddTransportInfo(TransportInfo(content_name, transport_desc));
  return RTCError::OK();
}

// Add the RTP description to the SessionDescription.
// If media_description_options.codecs_to_include is set, those codecs are used.
//
// If it is not set, the codecs used are computed based on:
// `codecs` = set of all possible codecs that can be used, with correct
// payload type mappings
//
// `supported_codecs` = set of codecs that are supported for the direction
// of this m= section
// `current_content` = current description, may be null.
// current_content->codecs() = set of previously negotiated codecs for this m=
// section
//
// The payload types should come from codecs, but the order should come
// from current_content->codecs() and then supported_codecs, to ensure that
// re-offers don't change existing codec priority, and that new codecs are added
// with the right priority.
RTCError MediaSessionDescriptionFactory::AddRtpContentForOffer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const ContentInfo* current_content,
    const SessionDescription* current_description,
    const RtpHeaderExtensions& header_extensions,
    StreamParamsVec* current_streams,
    SessionDescription* session_description,
    IceCredentialsIterator* ice_credentials) const {
  RTC_DCHECK(media_description_options.type == MediaType::AUDIO ||
             media_description_options.type == MediaType::VIDEO);

  std::vector<Codec> codecs_to_include;
  std::string mid = media_description_options.mid;
  RTCErrorOr<std::vector<Codec>> error_or_filtered_codecs =
      codec_lookup_helper_->GetCodecVendor()->GetNegotiatedCodecsForOffer(
          media_description_options, session_options, current_content,
          *codec_lookup_helper_->PayloadTypeSuggester());
  if (!error_or_filtered_codecs.ok()) {
    return error_or_filtered_codecs.MoveError();
  }

  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();

  codecs_to_include = error_or_filtered_codecs.MoveValue();
  std::unique_ptr<MediaContentDescription> content_description;
  if (media_description_options.type == MediaType::AUDIO) {
    content_description = std::make_unique<AudioContentDescription>();
  } else {
    content_description = std::make_unique<VideoContentDescription>();
  }
  // RFC 8888 support.
  content_description->set_rtcp_fb_ack_ccfb(offer_rfc_8888_);

  auto error = CreateMediaContentOffer(
      media_description_options, session_options, codecs_to_include,
      header_extensions, ssrc_generator(), current_streams,
      content_description.get(), transport_desc_factory_->trials());
  if (!error.ok()) {
    return error;
  }

  // Insecure transport should only occur in testing.
  bool secure_transport = !(transport_desc_factory_->insecure());
  SetMediaProtocol(secure_transport, content_description.get());

  content_description->set_direction(media_description_options.direction);
  content_description->set_sframe_enabled(
      media_description_options.sframe_enabled);
  bool has_codecs = !content_description->codecs().empty();

  session_description->AddContent(
      media_description_options.mid, MediaProtocolType::kRtp,
      media_description_options.stopped || !has_codecs,
      std::move(content_description));
  return AddTransportOffer(media_description_options.mid,
                           media_description_options.transport_options,
                           current_description, session_description,
                           ice_credentials);
}

RTCError MediaSessionDescriptionFactory::AddDataContentForOffer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const ContentInfo* current_content,
    const SessionDescription* current_description,
    StreamParamsVec* current_streams,
    SessionDescription* desc,
    IceCredentialsIterator* ice_credentials) const {
  auto data = std::make_unique<SctpDataContentDescription>();

  bool secure_transport = true;

  std::vector<std::string> crypto_suites;
  // Unlike SetMediaProtocol below, we need to set the protocol
  // before we call CreateMediaContentOffer. Otherwise,
  // CreateMediaContentOffer won't know this is SCTP and will
  // generate SSRCs rather than SIDs.
  data->set_protocol(secure_transport ? kMediaProtocolUdpDtlsSctp
                                      : kMediaProtocolSctp);
  data->set_use_sctpmap(session_options.use_obsolete_sctp_sdp);
  data->set_max_message_size(kSctpSendBufferSize);
  if (session_options.use_sctp_snap) {
    if (current_content && current_content->media_description()) {
      auto current_data_description =
          current_content->media_description()->as_sctp();
      RTC_DCHECK(current_data_description);
      data->set_sctp_init(current_data_description->sctp_init());
    } else {
      data->set_sctp_init(sctp_factory_->GenerateConnectionToken(env_));
    }
  }

  auto error = CreateContentOffer(
      media_description_options, session_options, RtpHeaderExtensions(),
      ssrc_generator(), current_streams, data.get(), env_.field_trials());
  if (!error.ok()) {
    return error;
  }

  desc->AddContent(media_description_options.mid, MediaProtocolType::kSctp,
                   media_description_options.stopped, std::move(data));
  return AddTransportOffer(media_description_options.mid,
                           media_description_options.transport_options,
                           current_description, desc, ice_credentials);
}

RTCError MediaSessionDescriptionFactory::AddUnsupportedContentForOffer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const ContentInfo* current_content,
    const SessionDescription* current_description,
    SessionDescription* desc,
    IceCredentialsIterator* ice_credentials) const {
  RTC_CHECK(IsMediaContentOfType(current_content, MediaType::UNSUPPORTED));

  const UnsupportedContentDescription* current_unsupported_description =
      current_content->media_description()->as_unsupported();
  auto unsupported = std::make_unique<UnsupportedContentDescription>(
      current_unsupported_description->media_type());
  unsupported->set_protocol(current_content->media_description()->protocol());
  desc->AddContent(media_description_options.mid, MediaProtocolType::kOther,
                   /*rejected=*/true, std::move(unsupported));

  return AddTransportOffer(media_description_options.mid,
                           media_description_options.transport_options,
                           current_description, desc, ice_credentials);
}

// `codecs` = set of all possible codecs that can be used, with correct
// payload type mappings
//
// `supported_codecs` = set of codecs that are supported for the direction
// of this m= section
//
// mcd->codecs() = set of previously negotiated codecs for this m= section
//
// The payload types should come from codecs, but the order should come
// from mcd->codecs() and then supported_codecs, to ensure that re-offers don't
// change existing codec priority, and that new codecs are added with the right
// priority.
RTCError MediaSessionDescriptionFactory::AddRtpContentForAnswer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const ContentInfo* offer_content,
    const SessionDescription* offer_description,
    const ContentInfo* current_content,
    const SessionDescription* current_description,
    const TransportInfo* bundle_transport,
    const RtpHeaderExtensions& header_extensions,
    StreamParamsVec* current_streams,
    SessionDescription* answer,
    bool include_ccfb_in_answer,
    IceCredentialsIterator* ice_credentials) const {
  RTC_DCHECK(media_description_options.type == MediaType::AUDIO ||
             media_description_options.type == MediaType::VIDEO);
  RTC_CHECK(
      IsMediaContentOfType(offer_content, media_description_options.type));
  const RtpMediaContentDescription* offer_content_description;
  if (media_description_options.type == MediaType::AUDIO) {
    offer_content_description = offer_content->media_description()->as_audio();
  } else {
    offer_content_description = offer_content->media_description()->as_video();
  }
  // If this section is part of a bundle, bundle_transport is non-null.
  // Then require_transport_attributes is false - we can handle sections
  // without the DTLS parameters. For rejected m-lines it does not matter.
  // Otherwise, transport attributes MUST be present.
  std::unique_ptr<TransportDescription> transport = CreateTransportAnswer(
      media_description_options.mid, offer_description,
      media_description_options.transport_options, current_description,
      !offer_content->rejected && bundle_transport == nullptr, ice_credentials);
  if (!transport) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INTERNAL_ERROR)
        << "Failed to create transport answer, transport is missing");
  }

  // Pick codecs based on the requested communications direction in the offer
  // and the selected direction in the answer.
  // Note these will be filtered one final time in CreateMediaContentAnswer.
  auto wants_rtd = media_description_options.direction;
  auto offer_rtd = offer_content_description->direction();
  auto answer_rtd = NegotiateRtpTransceiverDirection(offer_rtd, wants_rtd);

  std::vector<Codec> codecs_to_include;
  RTCErrorOr<std::vector<Codec>> error_or_filtered_codecs =
      codec_lookup_helper_->GetCodecVendor()->GetNegotiatedCodecsForAnswer(
          media_description_options, session_options, offer_rtd, answer_rtd,
          current_content, offer_content_description->codecs(),
          *codec_lookup_helper_->PayloadTypeSuggester());
  if (!error_or_filtered_codecs.ok()) {
    return error_or_filtered_codecs.MoveError();
  }

  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();

  codecs_to_include = error_or_filtered_codecs.MoveValue();
  // Determine if we have media codecs in common.
  bool has_usable_media_codecs =
      std::find_if(codecs_to_include.begin(), codecs_to_include.end(),
                   [](const Codec& c) {
                     return c.IsMediaCodec() && !IsComfortNoiseCodec(c);
                   }) != codecs_to_include.end();

  bool bundle_enabled = offer_description->HasGroup(GROUP_TYPE_BUNDLE) &&
                        session_options.bundle_enabled;
  std::unique_ptr<MediaContentDescription> answer_content;
  if (media_description_options.type == MediaType::AUDIO) {
    answer_content = std::make_unique<AudioContentDescription>();
  } else {
    answer_content = std::make_unique<VideoContentDescription>();
  }
  // RFC 8888 support. Only answer with "ack ccfb" if offer has it and
  // experiment is enabled.
  if (offer_content_description->rtcp_fb_ack_ccfb()) {
    if (accept_offer_with_rfc_8888_ && include_ccfb_in_answer) {
      answer_content->set_rtcp_fb_ack_ccfb(true);
      for (auto& codec : codecs_to_include) {
        codec.feedback_params.Remove(FeedbackParam(kRtcpFbParamTransportCc));
      }
    }
  }
  if (!SetCodecsInAnswer(offer_content_description, codecs_to_include,
                         media_description_options, session_options,
                         ssrc_generator(), current_streams,
                         answer_content.get(),
                         transport_desc_factory_->trials())) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to set codecs in answer");
  }
  RTC_DCHECK(codec_lookup_helper_->PayloadTypeSuggester());
  if (!CreateMediaContentAnswer(
          offer_content_description, media_description_options, session_options,
          filtered_rtp_header_extensions(header_extensions), ssrc_generator(),
          enable_encrypted_rtp_header_extensions_, current_streams,
          bundle_enabled, answer_content.get(),
          *codec_lookup_helper_->PayloadTypeSuggester(),
          offer_description->extmap_allow_mixed()
              ? RtpTransceiverIdDomain::kTwoByteAllowed
              : RtpTransceiverIdDomain::kOneByteOnly,
          env_.field_trials())) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to create answer");
  }

  bool secure = bundle_transport ? bundle_transport->description.secure()
                                 : transport->secure();
  bool rejected = media_description_options.stopped ||
                  offer_content->rejected || !has_usable_media_codecs ||
                  !IsMediaProtocolSupported(MediaType::AUDIO,
                                            answer_content->protocol(), secure);
  if (rejected) {
    RTC_LOG(LS_INFO) << "m= section '" << media_description_options.mid
                     << "' being rejected in answer.";
  }

  auto error =
      AddTransportAnswer(media_description_options.mid, *transport, answer);
  if (!error.ok()) {
    return error;
  }

  answer->AddContent(media_description_options.mid, offer_content->type,
                     rejected, std::move(answer_content));
  return RTCError::OK();
}

RTCError MediaSessionDescriptionFactory::AddDataContentForAnswer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const ContentInfo* offer_content,
    const SessionDescription* offer_description,
    const ContentInfo* current_content,
    const SessionDescription* current_description,
    const TransportInfo* bundle_transport,
    StreamParamsVec* current_streams,
    SessionDescription* answer,
    IceCredentialsIterator* ice_credentials) const {
  std::unique_ptr<TransportDescription> data_transport = CreateTransportAnswer(
      media_description_options.mid, offer_description,
      media_description_options.transport_options, current_description,
      !offer_content->rejected && bundle_transport == nullptr, ice_credentials);
  if (!data_transport) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INTERNAL_ERROR)
        << "Failed to create transport answer, data transport is missing");
  }

  bool bundle_enabled = offer_description->HasGroup(GROUP_TYPE_BUNDLE) &&
                        session_options.bundle_enabled;
  RTC_CHECK(IsMediaContentOfType(offer_content, MediaType::DATA));
  std::unique_ptr<MediaContentDescription> data_answer;
  if (offer_content->media_description()->as_sctp()) {
    // SCTP data content
    data_answer = std::make_unique<SctpDataContentDescription>();
    const SctpDataContentDescription* offer_data_description =
        offer_content->media_description()->as_sctp();
    // Respond with the offerer's proto, whatever it is.
    data_answer->as_sctp()->set_protocol(offer_data_description->protocol());
    // Respond with our max message size or the remote max messsage size,
    // whichever is smaller.
    // 0 is treated specially - it means "I can accept any size". Since
    // we do not implement infinite size messages, reply with
    // kSctpSendBufferSize.
    if (offer_data_description->max_message_size() <= 0) {
      data_answer->as_sctp()->set_max_message_size(kSctpSendBufferSize);
    } else {
      data_answer->as_sctp()->set_max_message_size(std::min(
          offer_data_description->max_message_size(), kSctpSendBufferSize));
    }
    RTC_DCHECK(codec_lookup_helper_->PayloadTypeSuggester());
    if (!CreateMediaContentAnswer(
            offer_data_description, media_description_options, session_options,
            RtpHeaderExtensions(), ssrc_generator(),
            enable_encrypted_rtp_header_extensions_, current_streams,
            bundle_enabled, data_answer.get(),
            *codec_lookup_helper_->PayloadTypeSuggester(),
            offer_description->extmap_allow_mixed()
                ? RtpTransceiverIdDomain::kTwoByteAllowed
                : RtpTransceiverIdDomain::kOneByteOnly,
            env_.field_trials())) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                           << "Failed to create answer");
    }
    // Respond with sctpmap if the offer uses sctpmap.
    bool offer_uses_sctpmap = offer_data_description->use_sctpmap();
    data_answer->as_sctp()->set_use_sctpmap(offer_uses_sctpmap);

    // Respond with sctp-init if the offer had sctp-init.
    if (session_options.use_sctp_snap) {
      if (offer_data_description->sctp_init().has_value()) {
        if (current_content && current_content->media_description()) {
          auto current_data_description =
              current_content->media_description()->as_sctp();
          RTC_DCHECK(current_data_description);
          data_answer->as_sctp()->set_sctp_init(
              current_data_description->sctp_init());
        } else {
          data_answer->as_sctp()->set_sctp_init(
              sctp_factory_->GenerateConnectionToken(env_));
        }
      }
    }
  } else {
    RTC_DCHECK_NOTREACHED() << "Non-SCTP data content found";
  }

  bool secure = bundle_transport ? bundle_transport->description.secure()
                                 : data_transport->secure();

  bool rejected = media_description_options.stopped ||
                  offer_content->rejected ||
                  !IsMediaProtocolSupported(MediaType::DATA,
                                            data_answer->protocol(), secure);
  auto error = AddTransportAnswer(media_description_options.mid,
                                  *data_transport, answer);
  if (!error.ok()) {
    return error;
  }
  answer->AddContent(media_description_options.mid, offer_content->type,
                     rejected, std::move(data_answer));
  return RTCError::OK();
}

RTCError MediaSessionDescriptionFactory::AddUnsupportedContentForAnswer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const ContentInfo* offer_content,
    const SessionDescription* offer_description,
    const ContentInfo* current_content,
    const SessionDescription* current_description,
    const TransportInfo* bundle_transport,
    SessionDescription* answer,
    IceCredentialsIterator* ice_credentials) const {
  std::unique_ptr<TransportDescription> unsupported_transport =
      CreateTransportAnswer(
          media_description_options.mid, offer_description,
          media_description_options.transport_options, current_description,
          !offer_content->rejected && bundle_transport == nullptr,
          ice_credentials);
  if (!unsupported_transport) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to create transport answer, unsupported "
                            "transport is missing");
  }
  RTC_CHECK(IsMediaContentOfType(offer_content, MediaType::UNSUPPORTED));

  const UnsupportedContentDescription* offer_unsupported_description =
      offer_content->media_description()->as_unsupported();
  std::unique_ptr<MediaContentDescription> unsupported_answer =
      std::make_unique<UnsupportedContentDescription>(
          offer_unsupported_description->media_type());
  unsupported_answer->set_protocol(offer_unsupported_description->protocol());

  auto error = AddTransportAnswer(media_description_options.mid,
                                  *unsupported_transport, answer);
  if (!error.ok()) {
    return error;
  }

  answer->AddContent(media_description_options.mid, offer_content->type,
                     /*rejected=*/true, std::move(unsupported_answer));
  return RTCError::OK();
}

bool IsMediaContent(const ContentInfo* content) {
  return (content && (content->type == MediaProtocolType::kRtp ||
                      content->type == MediaProtocolType::kSctp));
}

bool IsAudioContent(const ContentInfo* content) {
  return IsMediaContentOfType(content, MediaType::AUDIO);
}

bool IsVideoContent(const ContentInfo* content) {
  return IsMediaContentOfType(content, MediaType::VIDEO);
}

bool IsDataContent(const ContentInfo* content) {
  return IsMediaContentOfType(content, MediaType::DATA);
}

bool IsUnsupportedContent(const ContentInfo* content) {
  return IsMediaContentOfType(content, MediaType::UNSUPPORTED);
}

const ContentInfo* GetFirstMediaContent(const ContentInfos& contents,
                                        MediaType media_type) {
  for (const ContentInfo& content : contents) {
    if (IsMediaContentOfType(&content, media_type)) {
      return &content;
    }
  }
  return nullptr;
}

const ContentInfo* GetFirstAudioContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MediaType::AUDIO);
}

const ContentInfo* GetFirstVideoContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MediaType::VIDEO);
}

const ContentInfo* GetFirstDataContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MediaType::DATA);
}

const ContentInfo* GetFirstMediaContent(const SessionDescription* sdesc,
                                        MediaType media_type) {
  if (sdesc == nullptr) {
    return nullptr;
  }

  return GetFirstMediaContent(sdesc->contents(), media_type);
}

const ContentInfo* GetFirstAudioContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MediaType::AUDIO);
}

const ContentInfo* GetFirstVideoContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MediaType::VIDEO);
}

const ContentInfo* GetFirstDataContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MediaType::DATA);
}

const MediaContentDescription* GetFirstMediaContentDescription(
    const SessionDescription* sdesc,
    MediaType media_type) {
  const ContentInfo* content = GetFirstMediaContent(sdesc, media_type);
  return (content ? content->media_description() : nullptr);
}

const AudioContentDescription* GetFirstAudioContentDescription(
    const SessionDescription* sdesc) {
  auto desc = GetFirstMediaContentDescription(sdesc, MediaType::AUDIO);
  return desc ? desc->as_audio() : nullptr;
}

const VideoContentDescription* GetFirstVideoContentDescription(
    const SessionDescription* sdesc) {
  auto desc = GetFirstMediaContentDescription(sdesc, MediaType::VIDEO);
  return desc ? desc->as_video() : nullptr;
}

const SctpDataContentDescription* GetFirstSctpDataContentDescription(
    const SessionDescription* sdesc) {
  auto desc = GetFirstMediaContentDescription(sdesc, MediaType::DATA);
  return desc ? desc->as_sctp() : nullptr;
}

//
// Non-const versions of the above functions.
//

ContentInfo* GetFirstMediaContent(ContentInfos* contents,
                                  MediaType media_type) {
  for (ContentInfo& content : *contents) {
    if (IsMediaContentOfType(&content, media_type)) {
      return &content;
    }
  }
  return nullptr;
}

ContentInfo* GetFirstAudioContent(ContentInfos* contents) {
  return GetFirstMediaContent(contents, MediaType::AUDIO);
}

ContentInfo* GetFirstVideoContent(ContentInfos* contents) {
  return GetFirstMediaContent(contents, MediaType::VIDEO);
}

ContentInfo* GetFirstDataContent(ContentInfos* contents) {
  return GetFirstMediaContent(contents, MediaType::DATA);
}

ContentInfo* GetFirstMediaContent(SessionDescription* sdesc,
                                  MediaType media_type) {
  if (sdesc == nullptr) {
    return nullptr;
  }

  return GetFirstMediaContent(&sdesc->contents(), media_type);
}

ContentInfo* GetFirstAudioContent(SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MediaType::AUDIO);
}

ContentInfo* GetFirstVideoContent(SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MediaType::VIDEO);
}

ContentInfo* GetFirstDataContent(SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MediaType::DATA);
}

MediaContentDescription* GetFirstMediaContentDescription(
    SessionDescription* sdesc,
    MediaType media_type) {
  ContentInfo* content = GetFirstMediaContent(sdesc, media_type);
  return (content ? content->media_description() : nullptr);
}

AudioContentDescription* GetFirstAudioContentDescription(
    SessionDescription* sdesc) {
  auto desc = GetFirstMediaContentDescription(sdesc, MediaType::AUDIO);
  return desc ? desc->as_audio() : nullptr;
}

VideoContentDescription* GetFirstVideoContentDescription(
    SessionDescription* sdesc) {
  auto desc = GetFirstMediaContentDescription(sdesc, MediaType::VIDEO);
  return desc ? desc->as_video() : nullptr;
}

SctpDataContentDescription* GetFirstSctpDataContentDescription(
    SessionDescription* sdesc) {
  auto desc = GetFirstMediaContentDescription(sdesc, MediaType::DATA);
  return desc ? desc->as_sctp() : nullptr;
}

}  // namespace webrtc
