/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_WEBRTC_SDP_PLANB_H_
#define API_WEBRTC_SDP_PLANB_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "media/base/stream_params.h"
#include "pc/session_description.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {

// draft-alvestrand-mmusic-msid-01
// a=msid-semantic: WMS
// This is a legacy field supported only for Plan B semantics.
inline constexpr absl::string_view kAttributeMsidSemantics = "msid-semantic";

// SSRC attributes parsed from `a=ssrc:` SDP lines.
// The string fields are views into the SDP message being parsed and must not
// outlive it.
struct SsrcInfo {
  uint32_t ssrc_id = 0;
  absl::string_view cname;
  // Plan B SSRC-level `a=ssrc:<id> msid:<stream_id> <track_id>` attributes.
  absl::string_view stream_id;
  absl::string_view track_id;
};

// Creates StreamParams tracks for Plan B or legacy (non-Unified-Plan) SSRC
// signaling where `a=msid` is not signaled at the media section level.
void CreatePlanBTracksFromSsrcInfos(const std::vector<SsrcInfo>& ssrc_infos,
                                    int msid_signaling,
                                    StreamParamsVec* tracks);

// Appends a Plan B SSRC-level `a=ssrc:<ssrc_id> msid:<stream_id> <track_id>`
// attribute line to `message`.
void AddPlanBSsrcMsidLine(uint32_t ssrc,
                          const StreamParams& track,
                          StringBuilder* os,
                          std::string* message);

// Parses the deprecated session-level `a=msid-semantic:` value and updates
// `desc` if WMS semantics are indicated.
void ParsePlanBMsidSemantics(absl::string_view semantics,
                             SessionDescription* desc);

// Appends the deprecated session-level `a=msid-semantic: WMS ...` attribute
// line to `message` if `desc.msid_signaling()` is active.
void SerializePlanBMsidSemantics(const SessionDescription& desc,
                                 StringBuilder* os,
                                 std::string* message);

}  // namespace webrtc

#endif  // API_WEBRTC_SDP_PLANB_H_
