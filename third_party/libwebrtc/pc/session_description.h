/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_SESSION_DESCRIPTION_H_
#define PC_SESSION_DESCRIPTION_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/media_types.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/base/rid_description.h"
#include "media/base/stream_params.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "pc/media_protocol_names.h"
#include "pc/simulcast_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

using RtpHeaderExtensions = std::vector<RtpExtension>;

// Options to control how session descriptions are generated.
const int kAutoBandwidth = -1;

class AudioContentDescription;
class VideoContentDescription;
class SctpDataContentDescription;
class UnsupportedContentDescription;

// Describes a session description media section. There are subclasses for each
// media type (audio, video, data) that will have additional information.
class MediaContentDescription {
 public:
  MediaContentDescription() = default;
  virtual ~MediaContentDescription() = default;

  virtual MediaType type() const = 0;

  // Try to cast this media description to an AudioContentDescription. Returns
  // nullptr if the cast fails.
  virtual AudioContentDescription* as_audio() { return nullptr; }
  virtual const AudioContentDescription* as_audio() const { return nullptr; }

  // Try to cast this media description to a VideoContentDescription. Returns
  // nullptr if the cast fails.
  virtual VideoContentDescription* as_video() { return nullptr; }
  virtual const VideoContentDescription* as_video() const { return nullptr; }

  virtual SctpDataContentDescription* as_sctp() { return nullptr; }
  virtual const SctpDataContentDescription* as_sctp() const { return nullptr; }

  virtual UnsupportedContentDescription* as_unsupported() { return nullptr; }
  virtual const UnsupportedContentDescription* as_unsupported() const {
    return nullptr;
  }

  // Copy operator that returns an unique_ptr.
  // Not a virtual function.
  // If a type-specific variant of Clone() is desired, override it, or
  // simply use std::make_unique<typename>(*this) instead of Clone().
  std::unique_ptr<MediaContentDescription> Clone() const {
    return absl::WrapUnique(CloneInternal());
  }

  // `protocol` is the expected media transport protocol, such as RTP/AVPF,
  // RTP/SAVPF or SCTP/DTLS.
  const std::string& protocol() const { return protocol_; }
  virtual void set_protocol(absl::string_view protocol) {
    protocol_ = std::string(protocol);
  }

  RtpTransceiverDirection direction() const { return direction_; }
  void set_direction(RtpTransceiverDirection direction) {
    direction_ = direction;
  }

  bool rtcp_mux() const { return rtcp_mux_; }
  void set_rtcp_mux(bool mux) { rtcp_mux_ = mux; }

  bool rtcp_reduced_size() const { return rtcp_reduced_size_; }
  void set_rtcp_reduced_size(bool reduced_size) {
    rtcp_reduced_size_ = reduced_size;
  }

  // Whether RFC 3611 rcvr-rtt (receiver reference time report) was
  // negotiated, enabling non-sender RTT (RRTR/DLRR) on this m-section.
  bool receive_non_sender_rtt() const { return receive_non_sender_rtt_; }
  void set_receive_non_sender_rtt(bool enable) {
    receive_non_sender_rtt_ = enable;
  }

  // Indicates support for the remote network estimate packet type. This
  // functionality is experimental and subject to change without notice.
  bool remote_estimate() const { return remote_estimate_; }
  void set_remote_estimate(bool remote_estimate) {
    remote_estimate_ = remote_estimate;
  }

  // Support of RFC 8888 feedback messages.
  // This is a transport-wide property, but is signalled in SDP
  // at the m-line level; its mux category is IDENTICAL-PER-PT,
  // and only wildcard is allowed. RFC 8888 section 6.
  // In an answer, only one of rtcp_fb_ack_transport_cc() and rtcp_fb_ack_ccfb()
  // can be true.
  bool rtcp_fb_ack_ccfb() const { return rtcp_fb_ack_ccfb_; }
  void set_rtcp_fb_ack_ccfb(bool enable) { rtcp_fb_ack_ccfb_ = enable; }

  // Support of Transport-wide congestion control feedback.
  // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
  // In an answer, only one of rtcp_fb_ack_transport_cc() and rtcp_fb_ack_ccfb()
  // can be true.
  bool rtcp_fb_ack_transport_cc() const {
    for (const auto& codec : codecs_) {
      if (codec.feedback_params.Has(FeedbackParam(kRtcpFbParamTransportCc))) {
        return true;
      }
    }
    return false;
  }

  // Returns the preferred RTCP ack type used for congestion control for this
  // media content or `std::nullopt` if no supported type exists.
  std::optional<RtcpFeedbackType> preferred_rtcp_cc_ack_type() const {
    if (rtcp_fb_ack_ccfb_) {
      return RtcpFeedbackType::CCFB;
    }
    if (rtcp_fb_ack_transport_cc()) {
      return RtcpFeedbackType::TRANSPORT_CC;
    }
    return std::nullopt;
  }

  int bandwidth() const { return bandwidth_; }
  void set_bandwidth(int bandwidth) { bandwidth_ = bandwidth; }
  const std::string& bandwidth_type() const { return bandwidth_type_; }
  void set_bandwidth_type(std::string bandwidth_type) {
    bandwidth_type_ = std::move(bandwidth_type);
  }

  // List of RTP header extensions. URIs are **NOT** guaranteed to be unique
  // as they can appear twice when both encrypted and non-encrypted extensions
  // are present.
  // Use RtpExtension::FindHeaderExtensionByUri for finding and
  // RtpExtension::DeduplicateHeaderExtensions for filtering.
  const RtpHeaderExtensions& rtp_header_extensions() const {
    return rtp_header_extensions_;
  }
  void set_rtp_header_extensions(const RtpHeaderExtensions& extensions) {
    rtp_header_extensions_ = extensions;
  }
  void AddRtpHeaderExtension(const RtpExtension& ext) {
    rtp_header_extensions_.push_back(ext);
  }
  const StreamParamsVec& streams() const { return send_streams_; }
  // TODO(pthatcher): Remove this by giving mediamessage.cc access
  // to MediaContentDescription
  StreamParamsVec& mutable_streams() { return send_streams_; }
  void AddStream(const StreamParams& stream) {
    send_streams_.push_back(stream);
  }
  // Legacy streams have an ssrc, but nothing else.
  void AddLegacyStream(uint32_t ssrc) {
    AddStream(StreamParams::CreateLegacy(ssrc));
  }
  void AddLegacyStream(uint32_t ssrc, uint32_t fid_ssrc) {
    StreamParams sp = StreamParams::CreateLegacy(ssrc);
    sp.AddFidSsrc(ssrc, fid_ssrc);
    AddStream(sp);
  }

  uint32_t first_ssrc() const {
    if (send_streams_.empty()) {
      return 0;
    }
    return send_streams_[0].first_ssrc();
  }
  bool has_ssrcs() const {
    if (send_streams_.empty()) {
      return false;
    }
    return send_streams_[0].has_ssrcs();
  }

  void set_conference_mode(bool enable) { conference_mode_ = enable; }
  bool conference_mode() const { return conference_mode_; }

  // https://tools.ietf.org/html/rfc4566#section-5.7
  // May be present at the media or session level of SDP. If present at both
  // levels, the media-level attribute overwrites the session-level one.
  void set_connection_address(const SocketAddress& address) {
    connection_address_ = address;
  }
  const SocketAddress& connection_address() const {
    return connection_address_;
  }

  // Used to indicate at which level (session or media) an attribute is
  // signaled in the SDP.
  enum class AttributeLevel { kNone, kSession, kMedia };

  // Determines if it's allowed to mix one- and two-byte rtp header extensions
  // within the same rtp stream.
  void set_extmap_allow_mixed_level(AttributeLevel level) {
    if (level == AttributeLevel::kMedia &&
        extmap_allow_mixed_level_ == AttributeLevel::kSession) {
      // Do not downgrade from session level to media level.
      return;
    }
    extmap_allow_mixed_level_ = level;
  }
  AttributeLevel extmap_allow_mixed_level() const {
    return extmap_allow_mixed_level_;
  }
  bool extmap_allow_mixed() const {
    return extmap_allow_mixed_level_ != AttributeLevel::kNone;
  }

  // TODO(bugs.webrtc.org/455813732): Remove once downstream projects have
  // migrated to AttributeLevel and set_extmap_allow_mixed_level() /
  // extmap_allow_mixed_level().
  enum [[deprecated("Use AttributeLevel")]] ExtmapAllowMixed {
    kNo,
    kSession,
    kMedia
  };
  [[deprecated("Use set_extmap_allow_mixed_level")]]
  void set_extmap_allow_mixed_enum(ExtmapAllowMixed new_extmap_allow_mixed) {
    AttributeLevel level = AttributeLevel::kNone;
    switch (new_extmap_allow_mixed) {
      case kNo:
        level = AttributeLevel::kNone;
        break;
      case kSession:
        level = AttributeLevel::kSession;
        break;
      case kMedia:
        level = AttributeLevel::kMedia;
        break;
    }
    set_extmap_allow_mixed_level(level);
  }
  [[deprecated("Use extmap_allow_mixed_level")]]
  ExtmapAllowMixed extmap_allow_mixed_enum() const {
    switch (extmap_allow_mixed_level_) {
      case AttributeLevel::kNone:
        return kNo;
      case AttributeLevel::kSession:
        return kSession;
      case AttributeLevel::kMedia:
        return kMedia;
    }
  }

  // Simulcast functionality.
  bool HasSimulcast() const {
    // In practice this is only supported for video, but currently
    // tests populate the simulcast_ field for non video types.
    return !simulcast_.empty();
  }
  SimulcastDescription& simulcast_description() { return simulcast_; }
  const SimulcastDescription& simulcast_description() const {
    // In practice this is only supported for video, but currently
    // tests populate the simulcast_ field for non video types.
    return simulcast_;
  }
  void set_simulcast_description(const SimulcastDescription& simulcast) {
    // In practice only applies to video descriptions.
    simulcast_ = simulcast;
  }
  const std::vector<RidDescription>& receive_rids() const {
    return receive_rids_;
  }
  void set_receive_rids(const std::vector<RidDescription>& rids) {
    receive_rids_ = rids;
  }

  // Whether Sframe encryption is enabled for this media section.
  bool sframe_enabled() const { return sframe_enabled_; }
  void set_sframe_enabled(bool sframe_enabled) {
    sframe_enabled_ = sframe_enabled;
  }

  // Codecs should be in preference order (most preferred codec first).
  const std::vector<Codec>& codecs() const { return codecs_; }
  std::vector<Codec>& mutable_codecs() { return codecs_; }
  void set_codecs(const std::vector<Codec>& codecs) { codecs_ = codecs; }
  virtual bool has_codecs() const { return !codecs_.empty(); }
  bool HasCodec(int id) {
    return absl::c_find_if(codecs_, [id](const Codec& codec) {
             return codec.id == id;
           }) != codecs_.end();
  }
  void AddCodec(const Codec& codec) { codecs_.push_back(codec); }
  void AddOrReplaceCodec(const Codec& codec) {
    for (auto it = codecs_.begin(); it != codecs_.end(); ++it) {
      if (it->id == codec.id) {
        *it = codec;
        return;
      }
    }
    AddCodec(codec);
  }
  void AddCodecs(const std::vector<Codec>& codecs) {
    for (const auto& codec : codecs) {
      AddCodec(codec);
    }
  }

  // Determines if cryptex header extension encryption is supported.
  void set_cryptex_level(AttributeLevel level) {
    if (level == AttributeLevel::kMedia &&
        cryptex_level_ == AttributeLevel::kSession) {
      // Do not downgrade from session level to media level.
      return;
    }
    cryptex_level_ = level;
  }
  AttributeLevel cryptex_level() const { return cryptex_level_; }
  bool cryptex() const { return cryptex_level_ != AttributeLevel::kNone; }

 protected:
  // TODO(bugs.webrtc.org/15214): move all RTP related things to
  // RtpMediaDescription that the SCTP content description does
  // not inherit from.
  std::string protocol_;

 private:
  bool rtcp_mux_ = false;
  bool rtcp_reduced_size_ = false;
  bool receive_non_sender_rtt_ = false;
  bool remote_estimate_ = false;
  bool rtcp_fb_ack_ccfb_ = false;
  int bandwidth_ = kAutoBandwidth;
  std::string bandwidth_type_{kApplicationSpecificBandwidth};

  std::vector<RtpExtension> rtp_header_extensions_;
  StreamParamsVec send_streams_;
  bool conference_mode_ = false;
  RtpTransceiverDirection direction_ = RtpTransceiverDirection::kSendRecv;
  SocketAddress connection_address_;
  AttributeLevel extmap_allow_mixed_level_ = AttributeLevel::kMedia;

  SimulcastDescription simulcast_;
  std::vector<RidDescription> receive_rids_;
  bool sframe_enabled_ = false;

  // Copy function that returns a raw pointer. Caller will assert ownership.
  // Should only be called by the Clone() function. Must be implemented
  // by each final subclass.
  virtual MediaContentDescription* CloneInternal() const = 0;

  std::vector<Codec> codecs_;

  AttributeLevel cryptex_level_ = AttributeLevel::kNone;
};

class RtpMediaContentDescription : public MediaContentDescription {};

class AudioContentDescription final : public RtpMediaContentDescription {
 public:
  void set_protocol(absl::string_view protocol) override {
    RTC_DCHECK(IsRtpProtocol(protocol));
    protocol_ = std::string(protocol);
  }
  MediaType type() const override { return MediaType::AUDIO; }
  AudioContentDescription* as_audio() override { return this; }
  const AudioContentDescription* as_audio() const override { return this; }

 private:
  AudioContentDescription* CloneInternal() const override {
    return new AudioContentDescription(*this);
  }
};

class VideoContentDescription final : public RtpMediaContentDescription {
 public:
  void set_protocol(absl::string_view protocol) override {
    RTC_DCHECK(IsRtpProtocol(protocol));
    protocol_ = std::string(protocol);
  }
  MediaType type() const override { return MediaType::VIDEO; }
  VideoContentDescription* as_video() override { return this; }
  const VideoContentDescription* as_video() const override { return this; }

 private:
  VideoContentDescription* CloneInternal() const override {
    return new VideoContentDescription(*this);
  }
};

class SctpDataContentDescription final : public MediaContentDescription {
 public:
  SctpDataContentDescription() {}
  SctpDataContentDescription(const SctpDataContentDescription& o)
      : MediaContentDescription(o),
        use_sctpmap_(o.use_sctpmap_),
        port_(o.port_),
        max_message_size_(o.max_message_size_),
        sctp_init_(o.sctp_init_) {}
  MediaType type() const override { return MediaType::DATA; }
  SctpDataContentDescription* as_sctp() override { return this; }
  const SctpDataContentDescription* as_sctp() const override { return this; }

  bool has_codecs() const override { return false; }
  void set_protocol(absl::string_view protocol) override {
    RTC_DCHECK(IsSctpProtocol(protocol));
    protocol_ = std::string(protocol);
  }

  bool use_sctpmap() const { return use_sctpmap_; }
  void set_use_sctpmap(bool enable) { use_sctpmap_ = enable; }
  int port() const { return port_; }
  void set_port(int port) { port_ = port; }
  int max_message_size() const { return max_message_size_; }
  void set_max_message_size(int max_message_size) {
    max_message_size_ = max_message_size;
  }
  const std::optional<std::vector<uint8_t>>& sctp_init() const {
    return sctp_init_;
  }
  void set_sctp_init(std::optional<std::vector<uint8_t>> sctp_init) {
    sctp_init_ = std::move(sctp_init);
  }

 private:
  SctpDataContentDescription* CloneInternal() const override {
    return new SctpDataContentDescription(*this);
  }
  bool use_sctpmap_ = true;  // Note: "true" is no longer conformant.
  // Defaults should be constants imported from SCTP. Quick hack.
  int port_ = 5000;
  // draft-ietf-mmusic-sdp-sctp-23: Max message size default is 64K
  int max_message_size_ = 64 * 1024;

  // draft-hancke-tsvwg-snap
  std::optional<std::vector<uint8_t>> sctp_init_;
};

class UnsupportedContentDescription final : public MediaContentDescription {
 public:
  explicit UnsupportedContentDescription(absl::string_view media_type)
      : media_type_(media_type) {}
  MediaType type() const override { return MediaType::UNSUPPORTED; }

  UnsupportedContentDescription* as_unsupported() override { return this; }
  const UnsupportedContentDescription* as_unsupported() const override {
    return this;
  }

  bool has_codecs() const override { return false; }
  const std::string& media_type() const { return media_type_; }

 private:
  UnsupportedContentDescription* CloneInternal() const override {
    return new UnsupportedContentDescription(*this);
  }

  const std::string media_type_;
};

// Protocol used for encoding media. This is the "top level" protocol that may
// be wrapped by zero or many transport protocols (UDP, ICE, etc.).
enum class MediaProtocolType {
  kRtp,   // Section will use the RTP protocol (e.g., for audio or video).
          // https://tools.ietf.org/html/rfc3550
  kSctp,  // Section will use the SCTP protocol (e.g., for a data channel).
          // https://tools.ietf.org/html/rfc4960
  kOther  // Section will use another top protocol which is not
          // explicitly supported.
};

// Represents a session description section. Most information about the section
// is stored in the description, which is a subclass of MediaContentDescription.
// Owns the description.
class RTC_EXPORT ContentInfo final {
 public:
  ContentInfo(MediaProtocolType type,
              absl::string_view mid,
              std::unique_ptr<MediaContentDescription> description,
              bool rejected = false,
              bool bundle_only = false)
      : type(type),
        rejected(rejected),
        bundle_only(bundle_only),
        mid_(mid),
        description_(std::move(description)) {}
  ~ContentInfo();

  // Copy ctor and assignment will clone `description_`.
  ContentInfo(const ContentInfo& o);
  // Const ref assignment operator removed. Instead, use the explicit ctor.
  ContentInfo& operator=(const ContentInfo& o) = delete;

  ContentInfo(ContentInfo&& o) = default;
  ContentInfo& operator=(ContentInfo&& o) = default;

  const std::string& mid() const { return mid_; }
  void set_mid(absl::string_view mid) { mid_ = std::string(mid); }

  // Alias for `description`.
  MediaContentDescription* media_description();
  const MediaContentDescription* media_description() const;

  MediaProtocolType type;
  bool rejected = false;
  bool bundle_only = false;

 private:
  std::string mid_;
  std::unique_ptr<MediaContentDescription> description_;
};

using ContentNames = std::vector<std::string>;

// This class provides a mechanism to aggregate different media contents into a
// group. This group can also be shared with the peers in a pre-defined format.
// GroupInfo should be populated only with the `content_name` of the
// MediaDescription.
class ContentGroup {
 public:
  explicit ContentGroup(std::string semantics);
  ContentGroup(const ContentGroup&);
  ContentGroup(ContentGroup&&);
  ContentGroup& operator=(const ContentGroup&);
  ContentGroup& operator=(ContentGroup&&);
  bool operator==(const ContentGroup& o) const = default;
  ~ContentGroup();

  const std::string& semantics() const { return semantics_; }
  const ContentNames& content_names() const { return content_names_; }

  const std::string* FirstContentName() const;
  bool HasContentName(absl::string_view content_name) const;
  void AddContentName(absl::string_view content_name);
  bool RemoveContentName(absl::string_view content_name);
  // for debugging
  std::string ToString() const;

 private:
  std::string semantics_;
  ContentNames content_names_;
};

using ContentInfos = std::vector<ContentInfo>;
using ContentGroups = std::vector<ContentGroup>;

// Determines how the MSID will be signaled in the SDP.
// These can be used as bit flags to indicate both or the special value none.
enum MsidSignaling {
  // MSID is not signaled. This is not a bit flag and must be compared for
  // equality.
  kMsidSignalingNotUsed = 0x0,
  // Signal MSID with at least one a=msid line in the media section.
  // This requires unified plan.
  kMsidSignalingMediaSection = 0x1,
  // Signal MSID with a=ssrc: msid lines in the media section.
  // This should only be used with plan-b but is signalled in
  // offers for backward compability reasons.
  kMsidSignalingSsrcAttribute = 0x2,
  // Signal MSID with a=msid-semantic: WMS in the session section.
  // This is deprecated but signalled for backward compability reasons.
  // It is typically combined with 0x1 or 0x2.
  kMsidSignalingSemantic = 0x4
};

// Describes a collection of contents, each with its own name and
// type.  Analogous to a <jingle> or <session> stanza.  Assumes that
// contents are unique be name, but doesn't enforce that.
class SessionDescription {
 public:
  SessionDescription();
  ~SessionDescription();

  std::unique_ptr<SessionDescription> Clone() const;

  // Content accessors.
  const ContentInfos& contents() const { return contents_; }
  ContentInfos& contents() { return contents_; }
  const ContentInfo* GetContentByName(absl::string_view name) const;
  ContentInfo* GetContentByName(absl::string_view name);
  const MediaContentDescription* GetContentDescriptionByName(
      absl::string_view name) const;
  MediaContentDescription* GetContentDescriptionByName(absl::string_view name);
  const ContentInfo* FirstContentByType(MediaProtocolType type) const;
  const ContentInfo* FirstContent() const;

  // Content mutators.
  // Adds a content to this description. Takes ownership of ContentDescription*.
  void AddContent(absl::string_view name,
                  MediaProtocolType type,
                  std::unique_ptr<MediaContentDescription> description);
  void AddContent(absl::string_view name,
                  MediaProtocolType type,
                  bool rejected,
                  std::unique_ptr<MediaContentDescription> description);
  void AddContent(absl::string_view name,
                  MediaProtocolType type,
                  bool rejected,
                  bool bundle_only,
                  std::unique_ptr<MediaContentDescription> description);
  void AddContent(ContentInfo&& content);

  bool RemoveContentByName(absl::string_view name);

  // Transport accessors.
  const TransportInfos& transport_infos() const { return transport_infos_; }
  TransportInfos& transport_infos() { return transport_infos_; }
  const TransportInfo* GetTransportInfoByName(absl::string_view name) const;
  TransportInfo* GetTransportInfoByName(absl::string_view name);
  const TransportDescription* GetTransportDescriptionByName(
      absl::string_view name) const {
    const TransportInfo* tinfo = GetTransportInfoByName(name);
    return tinfo ? &tinfo->description : NULL;
  }

  // Transport mutators.
  void set_transport_infos(const TransportInfos& transport_infos) {
    transport_infos_ = transport_infos;
  }
  // Adds a TransportInfo to this description.
  void AddTransportInfo(const TransportInfo& transport_info);
  bool RemoveTransportInfoByName(absl::string_view name);

  // Group accessors.
  const ContentGroups& groups() const { return content_groups_; }
  const ContentGroup* GetGroupByName(absl::string_view name) const;
  std::vector<const ContentGroup*> GetGroupsByName(
      absl::string_view name) const;
  bool HasGroup(absl::string_view name) const;

  // Group mutators.
  void AddGroup(const ContentGroup& group) { content_groups_.push_back(group); }
  // Remove the first group with the same semantics specified by `name`.
  void RemoveGroupByName(absl::string_view name);

  // Global attributes.
  // Determines how the MSIDs were/will be signaled. Flag value composed of
  // MsidSignaling bits (see enum above).
  void set_msid_signaling(int msid_signaling) {
    msid_signaling_ = msid_signaling;
  }
  int msid_signaling() const { return msid_signaling_; }

  // Determines if it's allowed to mix one- and two-byte rtp header extensions
  // within the same rtp stream.
  void set_extmap_allow_mixed(bool supported) {
    extmap_allow_mixed_ = supported;
    MediaContentDescription::AttributeLevel media_level_setting =
        supported ? MediaContentDescription::AttributeLevel::kSession
                  : MediaContentDescription::AttributeLevel::kNone;
    for (auto& content : contents_) {
      // Do not set to kNone if the current setting is kMedia.
      if (supported ||
          content.media_description()->extmap_allow_mixed_level() !=
              MediaContentDescription::AttributeLevel::kMedia) {
        content.media_description()->set_extmap_allow_mixed_level(
            media_level_setting);
      }
    }
  }
  bool extmap_allow_mixed() const { return extmap_allow_mixed_; }

  void set_cryptex(bool supported) {
    cryptex_ = supported;
    MediaContentDescription::AttributeLevel media_level_setting =
        supported ? MediaContentDescription::AttributeLevel::kSession
                  : MediaContentDescription::AttributeLevel::kNone;
    for (auto& content : contents_) {
      // Do not set to kNone if the current setting is kMedia.
      if (supported || content.media_description()->cryptex_level() !=
                           MediaContentDescription::AttributeLevel::kMedia) {
        content.media_description()->set_cryptex_level(media_level_setting);
      }
    }
  }
  bool cryptex() const { return cryptex_; }

 private:
  SessionDescription(const SessionDescription&);

  ContentInfos contents_;
  TransportInfos transport_infos_;
  ContentGroups content_groups_;
  int msid_signaling_ = kMsidSignalingMediaSection | kMsidSignalingSemantic;
  bool extmap_allow_mixed_ = true;
  bool cryptex_ = false;
};

// Indicates whether a session description was sent by the local client or
// received from the remote client.
enum ContentSource { CS_LOCAL, CS_REMOTE };

}  //  namespace webrtc

#endif  // PC_SESSION_DESCRIPTION_H_
