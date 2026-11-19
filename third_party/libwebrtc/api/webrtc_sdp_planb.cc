/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/webrtc_sdp_planb.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/media_types.h"
#include "media/base/stream_params.h"
#include "pc/session_description.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {
namespace {

constexpr absl::string_view kLineBreak = "\r\n";
constexpr absl::string_view kDefaultMsid = "default";
constexpr absl::string_view kNoStreamMsid = "-";
constexpr absl::string_view kMediaStreamSemantic = "WMS";
constexpr absl::string_view kAttributeSsrc = "ssrc";
constexpr absl::string_view kSsrcAttributeMsid = "msid";

bool IsMediaType(const MediaContentDescription* description, MediaType type) {
  return description != nullptr && description->type() == type;
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

void GetMediaStreamIds(const ContentInfo* content,
                       flat_set<std::string>* labels) {
  for (const StreamParams& stream_params :
       content->media_description()->streams()) {
    for (const std::string& stream_id : stream_params.stream_ids()) {
      labels->insert(stream_id);
    }
  }
}

}  // namespace

void CreatePlanBTracksFromSsrcInfos(const std::vector<SsrcInfo>& ssrc_infos,
                                    int msid_signaling,
                                    StreamParamsVec* tracks) {
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

void AddPlanBSsrcMsidLine(uint32_t ssrc,
                          const StreamParams& track,
                          StringBuilder* os,
                          std::string* message) {
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
  os->Clear();
  *os << "a=" << kAttributeSsrc << ":" << ssrc << " " << kSsrcAttributeMsid
      << ":" << stream_id << " " << track.id << kLineBreak;
  message->append(os->str());
}

void ParsePlanBMsidSemantics(absl::string_view semantics,
                             SessionDescription* desc) {
  if (absl::StrContainsIgnoreCase(semantics, kMediaStreamSemantic)) {
    desc->set_msid_signaling(kMsidSignalingSemantic);
  }
}

void SerializePlanBMsidSemantics(const SessionDescription& desc,
                                 StringBuilder* os,
                                 std::string* message) {
  // TODO: bugs.webrtc.org/10421 - Change to & kMsidSignalingSemantic
  // when we think it's safe to do so, so that we gradually fade out this old
  // line that was removed from the specification.
  if (desc.msid_signaling() == kMsidSignalingNotUsed) {
    return;
  }
  os->Clear();
  *os << "a=" << kAttributeMsidSemantics << ": " << kMediaStreamSemantic;

  // TODO: bugs.webrtc.org/10421 - this code only looks at the first
  // audio/video content. Fixing that might result in much larger SDP and the
  // msid-semantic line should eventually go away so this is not worth fixing.
  flat_set<std::string> media_stream_ids;
  const ContentInfo* audio_content = LegacyGetFirstAudioContent(desc);
  if (audio_content != nullptr) {
    GetMediaStreamIds(audio_content, &media_stream_ids);
  }

  const ContentInfo* video_content = LegacyGetFirstVideoContent(desc);
  if (video_content != nullptr) {
    GetMediaStreamIds(video_content, &media_stream_ids);
  }

  for (const std::string& id : media_stream_ids) {
    *os << " " << id;
  }
  *os << kLineBreak;
  message->append(os->str());
}

}  // namespace webrtc
