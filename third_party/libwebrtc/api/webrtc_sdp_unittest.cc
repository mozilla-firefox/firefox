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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/uma_metrics.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/base/rid_description.h"
#include "media/base/stream_params.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "pc/media_protocol_names.h"
#include "pc/media_session.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/message_digest.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_fingerprint.h"
#include "rtc_base/strings/string_builder.h"
#include "system_wrappers/include/metrics.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Property;
using ::testing::SizeIs;

constexpr uint32_t kDefaultSctpPort = 5000;
constexpr uint16_t kUnusualSctpPort = 9556;
constexpr char kSessionTime[] = "t=0 0\r\n";
constexpr uint32_t kCandidatePriority = 2130706432U;  // pref = 1.0
constexpr char kAttributeIceUfragVoice[] = "a=ice-ufrag:ufrag_voice\r\n";
constexpr char kAttributeIcePwdVoice[] = "a=ice-pwd:pwd_voice\r\n";
constexpr char kAttributeIceUfragVideo[] = "a=ice-ufrag:ufrag_video\r\n";
constexpr char kAttributeIcePwdVideo[] = "a=ice-pwd:pwd_video\r\n";
constexpr uint32_t kCandidateGeneration = 2;
constexpr char kCandidateFoundation1[] = "a0+B/1";
constexpr char kCandidateFoundation2[] = "a0+B/2";
constexpr char kCandidateFoundation3[] = "a0+B/3";
constexpr char kCandidateFoundation4[] = "a0+B/4";
constexpr char kFingerprint[] =
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n";
constexpr char kExtmapAllowMixed[] = "a=extmap-allow-mixed\r\n";
constexpr RtpHeaderExtensionId kExtmapId(1);
constexpr char kExtmapUri[] = "http://example.com/082005/ext.htm#ttime";
constexpr char kExtmap[] =
    "a=extmap:1 http://example.com/082005/ext.htm#ttime\r\n";
constexpr char kExtmapWithDirectionAndAttribute[] =
    "a=extmap:1/sendrecv http://example.com/082005/ext.htm#ttime a1 a2\r\n";
constexpr char kExtmapWithDirectionAndAttributeEncrypted[] =
    "a=extmap:1/sendrecv urn:ietf:params:rtp-hdrext:encrypt "
    "http://example.com/082005/ext.htm#ttime a1 a2\r\n";

constexpr uint8_t kIdentityDigest[] = {0x4A, 0xAD, 0xB9, 0xB1, 0x3F, 0x82, 0x18,
                                       0x3B, 0x54, 0x02, 0x12, 0xDF, 0x3E, 0x5D,
                                       0x49, 0x6B, 0x19, 0xE5, 0x7C, 0xAB};

constexpr char kDtlsSctp[] = "DTLS/SCTP";
constexpr char kUdpDtlsSctp[] = "UDP/DTLS/SCTP";
constexpr char kTcpDtlsSctp[] = "TCP/DTLS/SCTP";

constexpr char kMediaSectionMsidLine[] =
    "a=msid:local_stream_1 audio_track_id_1";
constexpr char kSsrcAttributeMsidLine[] =
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1";

struct CodecParams {
  int max_ptime;
  int ptime;
  int min_ptime;
  int sprop_stereo;
  int stereo;
  int useinband;
  int maxaveragebitrate;
};

// Reference sdp string
constexpr char kSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay "
    "generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n";

// SDP reference string without the candidates.
constexpr char kSdpString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n";

// draft-ietf-mmusic-sctp-sdp-03
constexpr char kSdpSctpDataChannelString[] =
    "m=application 9 UDP/DTLS/SCTP 5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:data_content_name\r\n"
    "a=sctpmap:5000 webrtc-datachannel 65535\r\n";

// draft-ietf-mmusic-sctp-sdp-12
// Note - this is invalid per draft-ietf-mmusic-sctp-sdp-26,
// since the separator after "sctp-port" needs to be a colon.
constexpr char kSdpSctpDataChannelStringWithSctpPort[] =
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "a=sctp-port 5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:data_content_name\r\n";

// draft-ietf-mmusic-sctp-sdp-26
constexpr char kSdpSctpDataChannelStringWithSctpColonPort[] =
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "a=sctp-port:5000\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:data_content_name\r\n";

constexpr char kSdpSctpDataChannelWithCandidatesString[] =
    "m=application 2345 UDP/DTLS/SCTP 5000\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:data_content_name\r\n"
    "a=sctpmap:5000 webrtc-datachannel 65535\r\n";

// draft-hancke-tsvwg-snap
// a=sctp-init:<base64("CookieMonster")>
constexpr char kSdpSctpDataChannelStringWithSctpInit[] =
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "a=sctp-port:5000\r\n"
    "a=sctp-init:Q29va2llTW9uc3Rlcg==\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_data\r\n"
    "a=ice-pwd:pwd_data\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=mid:data_content_name\r\n";

constexpr char kSdpConferenceString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS\r\n"
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=x-google-flag:conference\r\n"
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=x-google-flag:conference\r\n";

constexpr char kSdpSessionString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream\r\n";

constexpr char kSdpAudioString[] =
    "m=audio 9 RTP/SAVPF 111\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream audio_track_id_1\r\n";

constexpr char kSdpVideoString[] =
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream video_track_id_1\r\n";

// Reference sdp string using bundle-only.
constexpr char kBundleOnlySdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=group:BUNDLE audio_content_name video_content_name\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "m=video 0 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=bundle-only\r\n"
    "a=mid:video_content_name\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n";

// Plan B SDP reference string, with 2 streams, 2 audio tracks and 3 video
// tracks.
constexpr char kPlanBSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS local_stream_1 local_stream_2\r\n"
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1\r\n"
    "a=ssrc:4 cname:stream_2_cname\r\n"
    "a=ssrc:4 msid:local_stream_2 audio_track_id_2\r\n"
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay "
    "generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:video_content_name\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:2 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    "a=ssrc:3 msid:local_stream_1 video_track_id_1\r\n"
    "a=ssrc:5 cname:stream_2_cname\r\n"
    "a=ssrc:5 msid:local_stream_2 video_track_id_2\r\n"
    "a=ssrc:6 cname:stream_2_cname\r\n"
    "a=ssrc:6 msid:local_stream_2 video_track_id_3\r\n";

// Unified Plan SDP reference string, with 2 streams, 2 audio tracks and 3 video
// tracks.
constexpr char kUnifiedPlanSdpFullString[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    // Audio track 1, stream 1 (with candidates).
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    // Video track 1, stream 1 (with candidates).
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay "
    "generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:video_content_name\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc-group:FEC 2 3\r\n"
    "a=ssrc:2 cname:stream_1_cname\r\n"
    "a=ssrc:3 cname:stream_1_cname\r\n"
    // Audio track 2, stream 2.
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice_2\r\na=ice-pwd:pwd_voice_2\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name_2\r\n"
    "a=msid:local_stream_2 audio_track_id_2\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:4 cname:stream_2_cname\r\n"
    // Video track 2, stream 2.
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video_2\r\na=ice-pwd:pwd_video_2\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:video_content_name_2\r\n"
    "a=msid:local_stream_2 video_track_id_2\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:5 cname:stream_2_cname\r\n"
    // Video track 3, stream 2.
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video_3\r\na=ice-pwd:pwd_video_3\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:video_content_name_3\r\n"
    "a=msid:local_stream_2 video_track_id_3\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:6 cname:stream_2_cname\r\n";

// Unified Plan SDP reference string:
// - audio track 1 has 1 a=msid lines
// - audio track 2 has 2 a=msid lines
// - audio track 3 has 1 a=msid line with the special "-" marker signifying that
//   there are 0 media stream ids.
// This Unified Plan SDP represents a SDP that signals the msid using both
// a=msid and a=ssrc msid semantics.
constexpr char kUnifiedPlanSdpFullStringWithSpecialMsid[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    // Audio track 1, with 1 stream id.
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:1 cname:stream_1_cname\r\n"
    "a=ssrc:1 msid:local_stream_1 audio_track_id_1\r\n"
    // Audio track 2, with two stream ids.
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice_2\r\na=ice-pwd:pwd_voice_2\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name_2\r\n"
    "a=sendrecv\r\n"
    "a=msid:local_stream_1 audio_track_id_2\r\n"
    "a=msid:local_stream_2 audio_track_id_2\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:4 cname:stream_1_cname\r\n"
    // The support for Plan B msid signaling only includes the
    // first media stream id "local_stream_1."
    "a=ssrc:4 msid:local_stream_1 audio_track_id_2\r\n"
    // Audio track 3, with no stream ids.
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice_3\r\na=ice-pwd:pwd_voice_3\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name_3\r\n"
    "a=sendrecv\r\n"
    "a=msid:- audio_track_id_3\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    "a=ssrc:7 cname:stream_2_cname\r\n"
    "a=ssrc:7 msid:- audio_track_id_3\r\n";

// SDP string for unified plan without SSRCs
constexpr char kUnifiedPlanSdpFullStringNoSsrc[] =
    "v=0\r\n"
    "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS local_stream_1\r\n"
    // Audio track 1, stream 1 (with candidates).
    "m=audio 2345 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 74.125.127.126\r\n"
    "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1239 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
    "raddr 192.168.1.5 rport 2346 "
    "generation 2\r\n"
    "a=candidate:a0+B/3 2 udp 2130706432 74.125.127.126 2347 typ srflx "
    "raddr 192.168.1.5 rport 2348 "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:audio_content_name\r\n"
    "a=msid:local_stream_1 audio_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    // Video track 1, stream 1 (with candidates).
    "m=video 3457 RTP/SAVPF 120\r\n"
    "c=IN IP4 74.125.224.39\r\n"
    "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
    "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 2 udp 2130706432 ::1 1240 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host "
    "generation 2\r\n"
    "a=candidate:a0+B/4 2 udp 2130706432 74.125.224.39 3456 typ relay "
    "generation 2\r\n"
    "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
    "generation 2\r\n"
    "a=ice-ufrag:ufrag_video\r\na=ice-pwd:pwd_video\r\n"
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"

    "a=mid:video_content_name\r\n"
    "a=msid:local_stream_1 video_track_id_1\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    // Audio track 2, stream 2.
    "m=audio 9 RTP/SAVPF 111 103 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_voice_2\r\na=ice-pwd:pwd_voice_2\r\n"
    "a=mid:audio_content_name_2\r\n"
    "a=msid:local_stream_2 audio_track_id_2\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=rtcp-rsize\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    // Video track 2, stream 2.
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video_2\r\na=ice-pwd:pwd_video_2\r\n"
    "a=mid:video_content_name_2\r\n"
    "a=msid:local_stream_2 video_track_id_2\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    // Video track 3, stream 2.
    "m=video 9 RTP/SAVPF 120\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:ufrag_video_3\r\na=ice-pwd:pwd_video_3\r\n"
    "a=mid:video_content_name_3\r\n"
    "a=msid:local_stream_2 video_track_id_3\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

// One candidate reference string as per W3c spec.
// candidate:<blah> not a=candidate:<blah>CRLF
constexpr char kRawCandidate[] =
    "candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host generation 2";
// One candidate reference string.
constexpr char kSdpOneCandidate[] =
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
    "generation 2\r\n";

constexpr char kSdpTcpActiveCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype active generation 2";
constexpr char kSdpTcpPassiveCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype passive generation 2";
constexpr char kSdpTcpSOCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype so generation 2";
constexpr char kSdpTcpInvalidCandidate[] =
    "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9 typ host "
    "tcptype invalid generation 2";

// One candidate reference string with IPV6 address.
constexpr char kRawIPV6Candidate[] =
    "candidate:a0+B/1 1 udp 2130706432 "
    "abcd:abcd:abcd:abcd:abcd:abcd:abcd:abcd 1234 typ host generation 2";

// One candidate reference string.
constexpr char kSdpOneCandidateWithUfragPwd[] =
    "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host network_name"
    " eth0 ufrag user_rtp pwd password_rtp generation 2\r\n";

constexpr char kRawHostnameCandidate[] =
    "candidate:a0+B/1 1 udp 2130706432 a.test 1234 typ host generation 2";

// Session id and version
constexpr char kSessionId[] = "18446744069414584320";
constexpr char kSessionVersion[] = "18446462598732840960";

// ICE options.
constexpr char kIceOption1[] = "iceoption1";
constexpr char kIceOption2[] = "iceoption2";
constexpr char kIceOption3[] = "iceoption3";

// ICE ufrags/passwords.
constexpr char kUfragVoice[] = "ufrag_voice";
constexpr char kPwdVoice[] = "pwd_voice";
constexpr char kUfragVideo[] = "ufrag_video";
constexpr char kPwdVideo[] = "pwd_video";
constexpr char kUfragData[] = "ufrag_data";
constexpr char kPwdData[] = "pwd_data";

// Extra ufrags/passwords for extra unified plan m= sections.
constexpr char kUfragVoice2[] = "ufrag_voice_2";
constexpr char kPwdVoice2[] = "pwd_voice_2";
constexpr char kUfragVoice3[] = "ufrag_voice_3";
constexpr char kPwdVoice3[] = "pwd_voice_3";
constexpr char kUfragVideo2[] = "ufrag_video_2";
constexpr char kPwdVideo2[] = "pwd_video_2";
constexpr char kUfragVideo3[] = "ufrag_video_3";
constexpr char kPwdVideo3[] = "pwd_video_3";

// Content name
constexpr char kAudioContentName[] = "audio_content_name";
constexpr char kVideoContentName[] = "video_content_name";
constexpr char kDataContentName[] = "data_content_name";

// Extra content names for extra unified plan m= sections.
constexpr char kAudioContentName2[] = "audio_content_name_2";
constexpr char kAudioContentName3[] = "audio_content_name_3";
constexpr char kVideoContentName2[] = "video_content_name_2";
constexpr char kVideoContentName3[] = "video_content_name_3";

// MediaStream 1
constexpr char kStreamId1[] = "local_stream_1";
constexpr char kStream1Cname[] = "stream_1_cname";
constexpr char kAudioTrackId1[] = "audio_track_id_1";
constexpr uint32_t kAudioTrack1Ssrc = 1;
constexpr char kVideoTrackId1[] = "video_track_id_1";
constexpr uint32_t kVideoTrack1Ssrc1 = 2;
constexpr uint32_t kVideoTrack1Ssrc2 = 3;

// MediaStream 2
constexpr char kStreamId2[] = "local_stream_2";
constexpr char kStream2Cname[] = "stream_2_cname";
constexpr char kAudioTrackId2[] = "audio_track_id_2";
constexpr uint32_t kAudioTrack2Ssrc = 4;
constexpr char kVideoTrackId2[] = "video_track_id_2";
constexpr uint32_t kVideoTrack2Ssrc = 5;
constexpr char kVideoTrackId3[] = "video_track_id_3";
constexpr uint32_t kVideoTrack3Ssrc = 6;
constexpr char kAudioTrackId3[] = "audio_track_id_3";
constexpr uint32_t kAudioTrack3Ssrc = 7;

// Candidate
constexpr char kDummyMid[] = "dummy_mid";
constexpr int kDummyIndex = 123;

// Misc
constexpr SdpType kDummyType = SdpType::kOffer;

// Helper functions

// Serializes a cricket Candidate.
std::string SdpSerializeCandidate(const Candidate& candidate) {
  return candidate.ToCandidateAttribute(true);
}

std::string SdpSerializeCandidate(const IceCandidate& candidate) {
  return SdpSerializeCandidate(candidate.candidate());
}

// Creates a new session description with a supplied sdp, of type
// SdpType::kOffer (kDummyType).
std::unique_ptr<SessionDescriptionInterface> SdpDeserialize(
    absl::string_view sdp,
    SdpParseError* absl_nullable error = nullptr) {
  static_assert(kDummyType == SdpType::kOffer);
  return SdpDeserialize(SdpType::kOffer, sdp, error);
}

std::string SdpSerialize(
    const std::unique_ptr<SessionDescriptionInterface>& sd) {
  EXPECT_THAT(sd, NotNull());
  return sd ? SdpSerialize(*sd) : "";
}

// Add some extra `newlines` to the `message` after `line`.
void InjectAfter(const std::string& line,
                 const std::string& newlines,
                 std::string* message) {
  absl::StrReplaceAll({{line, line + newlines}}, message);
}

void Replace(const std::string& line,
             const std::string& newlines,
             std::string* message) {
  absl::StrReplaceAll({{line, newlines}}, message);
}

// Expect a parse failure on the line containing `bad_part` when attempting to
// parse `bad_sdp`.
void ExpectParseFailure(const std::string& bad_sdp,
                        const std::string& bad_part) {
  SdpParseError error;
  ASSERT_THAT(SdpDeserialize(bad_sdp, &error), IsNull());
  EXPECT_NE(std::string::npos, error.line.find(bad_part.c_str()))
      << "Did not find " << bad_part << " in " << error.line;
}

// Expect fail to parse kSdpFullString if replace `good_part` with `bad_part`.
void ExpectParseFailure(const char* good_part, const char* bad_part) {
  std::string bad_sdp = kSdpFullString;
  Replace(good_part, bad_part, &bad_sdp);
  ExpectParseFailure(bad_sdp, bad_part);
}

// Expect fail to parse kSdpFullString if add `newlines` after `injectpoint`.
void ExpectParseFailureWithNewLines(const std::string& injectpoint,
                                    const std::string& newlines,
                                    const std::string& bad_part) {
  std::string bad_sdp = kSdpFullString;
  InjectAfter(injectpoint, newlines, &bad_sdp);
  ExpectParseFailure(bad_sdp, bad_part);
}

void ReplaceDirection(RtpTransceiverDirection direction, std::string* message) {
  std::string new_direction;
  switch (direction) {
    case RtpTransceiverDirection::kInactive:
      new_direction = "a=inactive";
      break;
    case RtpTransceiverDirection::kSendOnly:
      new_direction = "a=sendonly";
      break;
    case RtpTransceiverDirection::kRecvOnly:
      new_direction = "a=recvonly";
      break;
    case RtpTransceiverDirection::kSendRecv:
      new_direction = "a=sendrecv";
      break;
    case RtpTransceiverDirection::kStopped:
    default:
      RTC_DCHECK_NOTREACHED();
      new_direction = "a=sendrecv";
      break;
  }
  Replace("a=sendrecv", new_direction, message);
}

void ReplaceRejected(bool audio_rejected,
                     bool video_rejected,
                     std::string* message) {
  if (audio_rejected) {
    Replace("m=audio 9", "m=audio 0", message);
    Replace(kAttributeIceUfragVoice, "", message);
    Replace(kAttributeIcePwdVoice, "", message);
  }
  if (video_rejected) {
    Replace("m=video 9", "m=video 0", message);
    Replace(kAttributeIceUfragVideo, "", message);
    Replace(kAttributeIcePwdVideo, "", message);
  }
}

TransportDescription MakeTransportDescription(std::string ufrag,
                                              std::string pwd) {
  SSLFingerprint fingerprint(DIGEST_SHA_1, kIdentityDigest);
  return TransportDescription(std::vector<std::string>(), ufrag, pwd,
                              ICEMODE_FULL, CONNECTIONROLE_NONE, &fingerprint);
}

std::unique_ptr<IceCandidate> NewCandidate(absl::string_view sdp,
                                           absl::string_view mid = kDummyMid,
                                           int index = kDummyIndex) {
  return IceCandidate::Create(mid, index, sdp);
}

// WebRtcSdpTest

class WebRtcSdpTest : public ::testing::Test {
 public:
  WebRtcSdpTest() {
    // AudioContentDescription
    audio_desc_ = CreateAudioContentDescription();
    StreamParams audio_stream;
    audio_stream.id = kAudioTrackId1;
    audio_stream.cname = kStream1Cname;
    audio_stream.set_stream_ids({kStreamId1});
    audio_stream.ssrcs.push_back(kAudioTrack1Ssrc);
    audio_desc_->AddStream(audio_stream);
    SocketAddress audio_addr("74.125.127.126", 2345);
    audio_desc_->set_connection_address(audio_addr);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp,
                     absl::WrapUnique(audio_desc_));

    // VideoContentDescription
    video_desc_ = CreateVideoContentDescription();
    StreamParams video_stream;
    video_stream.id = kVideoTrackId1;
    video_stream.cname = kStream1Cname;
    video_stream.set_stream_ids({kStreamId1});
    video_stream.ssrcs.push_back(kVideoTrack1Ssrc1);
    video_stream.ssrcs.push_back(kVideoTrack1Ssrc2);
    SsrcGroup ssrc_group(kFecSsrcGroupSemantics, video_stream.ssrcs);
    video_stream.ssrc_groups.push_back(ssrc_group);
    video_desc_->AddStream(video_stream);
    SocketAddress video_addr("74.125.224.39", 3457);
    video_desc_->set_connection_address(video_addr);
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp,
                     absl::WrapUnique(video_desc_));

    // TransportInfo, with fingerprint
    SSLFingerprint fingerprint(DIGEST_SHA_1, kIdentityDigest);
    desc_.AddTransportInfo(TransportInfo(
        kAudioContentName, MakeTransportDescription(kUfragVoice, kPwdVoice)));
    desc_.AddTransportInfo(TransportInfo(
        kVideoContentName, MakeTransportDescription(kUfragVideo, kPwdVideo)));

    // v4 host
    int port = 1234;
    SocketAddress address("192.168.1.5", port++);
    Candidate candidate1(ICE_CANDIDATE_COMPONENT_RTP, "udp", address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate2(ICE_CANDIDATE_COMPONENT_RTCP, "udp", address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate3(ICE_CANDIDATE_COMPONENT_RTCP, "udp", address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation1);
    address.SetPort(port++);
    Candidate candidate4(ICE_CANDIDATE_COMPONENT_RTP, "udp", address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation1);

    // v6 host
    SocketAddress v6_address("::1", port++);
    Candidate candidate5(ICE_CANDIDATE_COMPONENT_RTP, "udp", v6_address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    Candidate candidate6(ICE_CANDIDATE_COMPONENT_RTCP, "udp", v6_address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    Candidate candidate7(ICE_CANDIDATE_COMPONENT_RTCP, "udp", v6_address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation2);
    v6_address.SetPort(port++);
    Candidate candidate8(ICE_CANDIDATE_COMPONENT_RTP, "udp", v6_address,
                         kCandidatePriority, "", "", IceCandidateType::kHost,
                         kCandidateGeneration, kCandidateFoundation2);

    // stun
    int port_stun = 2345;
    SocketAddress address_stun("74.125.127.126", port_stun++);
    SocketAddress rel_address_stun("192.168.1.5", port_stun++);
    Candidate candidate9(ICE_CANDIDATE_COMPONENT_RTP, "udp", address_stun,
                         kCandidatePriority, "", "", IceCandidateType::kSrflx,
                         kCandidateGeneration, kCandidateFoundation3);
    candidate9.set_related_address(rel_address_stun);

    address_stun.SetPort(port_stun++);
    rel_address_stun.SetPort(port_stun++);
    Candidate candidate10(ICE_CANDIDATE_COMPONENT_RTCP, "udp", address_stun,
                          kCandidatePriority, "", "", IceCandidateType::kSrflx,
                          kCandidateGeneration, kCandidateFoundation3);
    candidate10.set_related_address(rel_address_stun);

    // relay
    int port_relay = 3456;
    SocketAddress address_relay("74.125.224.39", port_relay++);
    Candidate candidate11(ICE_CANDIDATE_COMPONENT_RTCP, "udp", address_relay,
                          kCandidatePriority, "", "", IceCandidateType::kRelay,
                          kCandidateGeneration, kCandidateFoundation4);
    address_relay.SetPort(port_relay++);
    Candidate candidate12(ICE_CANDIDATE_COMPONENT_RTP, "udp", address_relay,
                          kCandidatePriority, "", "", IceCandidateType::kRelay,
                          kCandidateGeneration, kCandidateFoundation4);

    // voice
    candidates_.push_back(candidate1);
    candidates_.push_back(candidate2);
    candidates_.push_back(candidate5);
    candidates_.push_back(candidate6);
    candidates_.push_back(candidate9);
    candidates_.push_back(candidate10);

    // video
    candidates_.push_back(candidate3);
    candidates_.push_back(candidate4);
    candidates_.push_back(candidate7);
    candidates_.push_back(candidate8);
    candidates_.push_back(candidate11);
    candidates_.push_back(candidate12);

    jcandidate_.reset(
        new IceCandidate(std::string("audio_content_name"), 0, candidate1));

    // Set up the main session description object.
    jdesc_ = NewSessionDescriptionWithCandidates();
  }

  void AddCandidatesToDescription(SessionDescriptionInterface* sd) {
    ASSERT_THAT(sd, NotNull());
    absl::string_view mline_id;
    int mline_index = 0;
    for (size_t i = 0; i < candidates_.size(); ++i) {
      // In this test, the audio m line index will be 0, and the video m line
      // will be 1.
      bool is_video = (i > 5);
      mline_id = is_video ? "video_content_name" : "audio_content_name";
      mline_index = is_video ? 1 : 0;
      IceCandidate jice(mline_id, mline_index, candidates_.at(i));
      sd->AddCandidate(&jice);
    }
  }

  // Creates a new SessionDescriptionInterface object from a clone of
  // the inner `desc_` description.
  std::unique_ptr<SessionDescriptionInterface>
  NewSessionDescriptionWithCandidates() {
    std::unique_ptr<SessionDescriptionInterface> sd = CreateSessionDescription(
        kDummyType, kSessionId, kSessionVersion, desc_.Clone());
    EXPECT_THAT(sd, NotNull());
    if (sd) {
      AddCandidatesToDescription(sd.get());
    }
    return sd;
  }

  // Creates a new session description based on the inner `desc_`
  // and does not add the default candidate set.
  std::unique_ptr<SessionDescriptionInterface>
  NewSessionDescriptionNoCandidates() {
    // Using the jdesc_->Clone() method would also clone the candidates, so
    // instead, we just create a new session description with the properties
    // from the inner jdesc_ object.
    return CreateSessionDescription(kDummyType, kSessionId, kSessionVersion,
                                    desc_.Clone());
  }

  void RemoveVideoCandidates(std::unique_ptr<SessionDescriptionInterface>& sd) {
    ASSERT_THAT(sd, NotNull());
    const IceCandidateCollection* video_candidates_collection =
        sd->candidates(1);
    ASSERT_NE(nullptr, video_candidates_collection);
    // Since this loop modifies video_candidates_collection, just loop until
    // it's empty instead of using a for loop.
    while (!video_candidates_collection->candidates().empty()) {
      ASSERT_TRUE(sd->RemoveCandidate(
          video_candidates_collection->candidates().back().get()));
    }
  }

  // Turns the existing reference description into a description using
  // a=bundle-only. This means no transport attributes and a 0 port value on
  // the m= sections not associated with the BUNDLE-tag.
  std::unique_ptr<SessionDescriptionInterface> MakeBundleOnlyDescription() {
    // And the rest of the transport attributes.
    desc_.transport_infos()[1].description.ice_ufrag.clear();
    desc_.transport_infos()[1].description.ice_pwd.clear();
    desc_.transport_infos()[1].description.connection_role =
        CONNECTIONROLE_NONE;

    // Set bundle-only flag.
    desc_.contents()[1].bundle_only = true;

    // Add BUNDLE group.
    ContentGroup group(GROUP_TYPE_BUNDLE);
    group.AddContentName(kAudioContentName);
    group.AddContentName(kVideoContentName);
    desc_.AddGroup(group);

    std::unique_ptr<SessionDescriptionInterface> sd =
        NewSessionDescriptionWithCandidates();
    // Remove the video candidates (not audio). This is to maintain
    // compatibility with the expectations of the tests that call
    // MakeBundleOnlyDescription().
    RemoveVideoCandidates(sd);
    return sd;
  }

  // Turns the existing reference description into a plan B description,
  // with 2 audio tracks and 3 video tracks.
  std::unique_ptr<SessionDescriptionInterface> MakePlanBDescription() {
    audio_desc_ = new AudioContentDescription(*audio_desc_);
    video_desc_ = new VideoContentDescription(*video_desc_);

    StreamParams audio_track_2;
    audio_track_2.id = kAudioTrackId2;
    audio_track_2.cname = kStream2Cname;
    audio_track_2.set_stream_ids({kStreamId2});
    audio_track_2.ssrcs.push_back(kAudioTrack2Ssrc);
    audio_desc_->AddStream(audio_track_2);

    StreamParams video_track_2;
    video_track_2.id = kVideoTrackId2;
    video_track_2.cname = kStream2Cname;
    video_track_2.set_stream_ids({kStreamId2});
    video_track_2.ssrcs.push_back(kVideoTrack2Ssrc);
    video_desc_->AddStream(video_track_2);

    StreamParams video_track_3;
    video_track_3.id = kVideoTrackId3;
    video_track_3.cname = kStream2Cname;
    video_track_3.set_stream_ids({kStreamId2});
    video_track_3.ssrcs.push_back(kVideoTrack3Ssrc);
    video_desc_->AddStream(video_track_3);

    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp,
                     absl::WrapUnique(audio_desc_));
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp,
                     absl::WrapUnique(video_desc_));
    desc_.set_msid_signaling(kMsidSignalingSsrcAttribute |
                             kMsidSignalingSemantic);
    return NewSessionDescriptionWithCandidates();
  }

  // Turns the existing reference description into a unified plan description,
  // with 2 audio tracks and 3 video tracks.
  void MakeUnifiedPlanDescription(bool use_ssrcs = true) {
    // Audio track 2.
    AudioContentDescription* audio_desc_2 = CreateAudioContentDescription();
    StreamParams audio_track_2;
    audio_track_2.id = kAudioTrackId2;
    audio_track_2.set_stream_ids({kStreamId2});
    if (use_ssrcs) {
      audio_track_2.cname = kStream2Cname;
      audio_track_2.ssrcs.push_back(kAudioTrack2Ssrc);
    }
    audio_desc_2->AddStream(audio_track_2);
    desc_.AddContent(kAudioContentName2, MediaProtocolType::kRtp,
                     absl::WrapUnique(audio_desc_2));
    desc_.AddTransportInfo(
        TransportInfo(kAudioContentName2,
                      MakeTransportDescription(kUfragVoice2, kPwdVoice2)));
    // Video track 2, in stream 2.
    VideoContentDescription* video_desc_2 = CreateVideoContentDescription();
    StreamParams video_track_2;
    video_track_2.id = kVideoTrackId2;
    video_track_2.set_stream_ids({kStreamId2});
    if (use_ssrcs) {
      video_track_2.cname = kStream2Cname;
      video_track_2.ssrcs.push_back(kVideoTrack2Ssrc);
    }
    video_desc_2->AddStream(video_track_2);
    desc_.AddContent(kVideoContentName2, MediaProtocolType::kRtp,
                     absl::WrapUnique(video_desc_2));
    desc_.AddTransportInfo(
        TransportInfo(kVideoContentName2,
                      MakeTransportDescription(kUfragVideo2, kPwdVideo2)));

    // Video track 3, in stream 2.
    VideoContentDescription* video_desc_3 = CreateVideoContentDescription();
    StreamParams video_track_3;
    video_track_3.id = kVideoTrackId3;
    video_track_3.set_stream_ids({kStreamId2});
    if (use_ssrcs) {
      video_track_3.cname = kStream2Cname;
      video_track_3.ssrcs.push_back(kVideoTrack3Ssrc);
    }
    video_desc_3->AddStream(video_track_3);
    desc_.AddContent(kVideoContentName3, MediaProtocolType::kRtp,
                     absl::WrapUnique(video_desc_3));
    desc_.AddTransportInfo(
        TransportInfo(kVideoContentName3,
                      MakeTransportDescription(kUfragVideo3, kPwdVideo3)));
    desc_.set_msid_signaling(kMsidSignalingMediaSection |
                             kMsidSignalingSemantic);

    jdesc_ = NewSessionDescriptionWithCandidates();
  }

  // Creates an audio content description with no streams, and some default
  // configuration.
  AudioContentDescription* CreateAudioContentDescription() {
    AudioContentDescription* audio = new AudioContentDescription();
    audio->set_rtcp_mux(true);
    audio->set_rtcp_reduced_size(true);
    audio->set_protocol(kMediaProtocolSavpf);
    audio->AddCodec(CreateAudioCodec(111, "opus", 48000, 2));
    audio->AddCodec(CreateAudioCodec(103, "ISAC", 16000, 1));
    audio->AddCodec(CreateAudioCodec(104, "ISAC", 32000, 1));
    return audio;
  }

  // Turns the existing reference description into a unified plan description,
  // with 3 audio MediaContentDescriptions with special StreamParams that
  // contain 0 or multiple stream ids: - audio track 1 has 1 media stream id -
  // audio track 2 has 2 media stream ids - audio track 3 has 0 media stream ids
  void MakeUnifiedPlanDescriptionMultipleStreamIds(const int msid_signaling) {
    desc_.RemoveContentByName(kVideoContentName);
    desc_.RemoveTransportInfoByName(kVideoContentName);
    RemoveVideoCandidates(jdesc_);

    // Audio track 2 has 2 media stream ids.
    AudioContentDescription* audio_desc_2 = CreateAudioContentDescription();
    StreamParams audio_track_2;
    audio_track_2.id = kAudioTrackId2;
    audio_track_2.cname = kStream1Cname;
    audio_track_2.set_stream_ids({kStreamId1, kStreamId2});
    audio_track_2.ssrcs.push_back(kAudioTrack2Ssrc);
    audio_desc_2->AddStream(audio_track_2);
    desc_.AddContent(kAudioContentName2, MediaProtocolType::kRtp,
                     absl::WrapUnique(audio_desc_2));
    desc_.AddTransportInfo(
        TransportInfo(kAudioContentName2,
                      MakeTransportDescription(kUfragVoice2, kPwdVoice2)));

    // Audio track 3 has no stream ids.
    AudioContentDescription* audio_desc_3 = CreateAudioContentDescription();
    StreamParams audio_track_3;
    audio_track_3.id = kAudioTrackId3;
    audio_track_3.cname = kStream2Cname;
    audio_track_3.set_stream_ids({});
    audio_track_3.ssrcs.push_back(kAudioTrack3Ssrc);
    audio_desc_3->AddStream(audio_track_3);
    desc_.AddContent(kAudioContentName3, MediaProtocolType::kRtp,
                     absl::WrapUnique(audio_desc_3));
    desc_.AddTransportInfo(
        TransportInfo(kAudioContentName3,
                      MakeTransportDescription(kUfragVoice3, kPwdVoice3)));
    desc_.set_msid_signaling(msid_signaling);
    jdesc_ = NewSessionDescriptionWithCandidates();
    ASSERT_THAT(jdesc_, NotNull());
  }

  // Creates a video content description with no streams, and some default
  // configuration.
  VideoContentDescription* CreateVideoContentDescription() {
    VideoContentDescription* video = new VideoContentDescription();
    video->set_protocol(kMediaProtocolSavpf);
    video->AddCodec(CreateVideoCodec(120, "VP8"));
    return video;
  }

  void CompareMediaContentDescription(const MediaContentDescription* cd1,
                                      const MediaContentDescription* cd2) {
    // type
    EXPECT_EQ(cd1->type(), cd2->type());

    // content direction
    EXPECT_EQ(cd1->direction(), cd2->direction());

    // rtcp_mux
    EXPECT_EQ(cd1->rtcp_mux(), cd2->rtcp_mux());

    // rtcp_reduced_size
    EXPECT_EQ(cd1->rtcp_reduced_size(), cd2->rtcp_reduced_size());

    // protocol
    // Use an equivalence class here, for old and new versions of the
    // protocol description.
    if (cd1->protocol() == kMediaProtocolDtlsSctp ||
        cd1->protocol() == kMediaProtocolUdpDtlsSctp ||
        cd1->protocol() == kMediaProtocolTcpDtlsSctp) {
      const bool cd2_is_also_dtls_sctp =
          cd2->protocol() == kMediaProtocolDtlsSctp ||
          cd2->protocol() == kMediaProtocolUdpDtlsSctp ||
          cd2->protocol() == kMediaProtocolTcpDtlsSctp;
      EXPECT_TRUE(cd2_is_also_dtls_sctp);
    } else {
      EXPECT_EQ(cd1->protocol(), cd2->protocol());
    }

    // codecs
    EXPECT_EQ(cd1->codecs(), cd2->codecs());

    // bandwidth
    EXPECT_EQ(cd1->bandwidth(), cd2->bandwidth());

    // streams
    EXPECT_EQ(cd1->streams(), cd2->streams());

    // extmap-allow-mixed
    EXPECT_EQ(cd1->extmap_allow_mixed_level(), cd2->extmap_allow_mixed_level());

    // extmap
    ASSERT_EQ(cd1->rtp_header_extensions().size(),
              cd2->rtp_header_extensions().size());
    for (size_t i = 0; i < cd1->rtp_header_extensions().size(); ++i) {
      const RtpExtension ext1 = cd1->rtp_header_extensions().at(i);
      const RtpExtension ext2 = cd2->rtp_header_extensions().at(i);
      EXPECT_EQ(ext1.uri, ext2.uri);
      EXPECT_EQ(ext1.id, ext2.id);
      EXPECT_EQ(ext1.encrypt, ext2.encrypt);
    }
  }

  void CompareRidDescriptionIds(const std::vector<RidDescription>& rids,
                                const std::vector<std::string>& ids) {
    // Order of elements does not matter, only equivalence of sets.
    EXPECT_EQ(rids.size(), ids.size());
    for (const std::string& id : ids) {
      EXPECT_EQ(1l, absl::c_count_if(rids, [id](const RidDescription& rid) {
                  return rid.rid == id;
                }));
    }
  }

  void CompareSimulcastDescription(const SimulcastDescription& simulcast1,
                                   const SimulcastDescription& simulcast2) {
    EXPECT_EQ(simulcast1.send_layers().size(), simulcast2.send_layers().size());
    EXPECT_EQ(simulcast1.receive_layers().size(),
              simulcast2.receive_layers().size());
  }

  void CompareSctpDataContentDescription(
      const SctpDataContentDescription* dcd1,
      const SctpDataContentDescription* dcd2) {
    EXPECT_EQ(dcd1->use_sctpmap(), dcd2->use_sctpmap());
    EXPECT_EQ(dcd1->port(), dcd2->port());
    EXPECT_EQ(dcd1->max_message_size(), dcd2->max_message_size());
  }

  void CompareSessionDescription(const SessionDescription& desc1,
                                 const SessionDescription& desc2) {
    // Compare content descriptions.
    if (desc1.contents().size() != desc2.contents().size()) {
      ADD_FAILURE();
      return;
    }
    for (size_t i = 0; i < desc1.contents().size(); ++i) {
      const ContentInfo& c1 = desc1.contents().at(i);
      const ContentInfo& c2 = desc2.contents().at(i);
      // ContentInfo properties.
      EXPECT_EQ(c1.mid(), c2.mid());
      EXPECT_EQ(c1.type, c2.type);
      EXPECT_EQ(c1.rejected, c2.rejected);
      EXPECT_EQ(c1.bundle_only, c2.bundle_only);

      ASSERT_EQ(IsAudioContent(&c1), IsAudioContent(&c2));
      if (IsAudioContent(&c1)) {
        CompareMediaContentDescription(c1.media_description(),
                                       c2.media_description());
      }

      ASSERT_EQ(IsVideoContent(&c1), IsVideoContent(&c2));
      if (IsVideoContent(&c1)) {
        CompareMediaContentDescription(c1.media_description(),
                                       c2.media_description());
      }

      ASSERT_EQ(IsDataContent(&c1), IsDataContent(&c2));
      if (c1.media_description()->as_sctp()) {
        ASSERT_TRUE(c2.media_description()->as_sctp());
        const SctpDataContentDescription* scd1 =
            c1.media_description()->as_sctp();
        const SctpDataContentDescription* scd2 =
            c2.media_description()->as_sctp();
        CompareSctpDataContentDescription(scd1, scd2);
      }

      CompareSimulcastDescription(
          c1.media_description()->simulcast_description(),
          c2.media_description()->simulcast_description());
    }

    // group
    const ContentGroups groups1 = desc1.groups();
    const ContentGroups groups2 = desc2.groups();
    EXPECT_EQ(groups1.size(), groups1.size());
    if (groups1.size() != groups2.size()) {
      ADD_FAILURE();
      return;
    }
    for (size_t i = 0; i < groups1.size(); ++i) {
      const ContentGroup group1 = groups1.at(i);
      const ContentGroup group2 = groups2.at(i);
      EXPECT_EQ(group1.semantics(), group2.semantics());
      const ContentNames names1 = group1.content_names();
      const ContentNames names2 = group2.content_names();
      EXPECT_EQ(names1.size(), names2.size());
      if (names1.size() != names2.size()) {
        ADD_FAILURE();
        return;
      }
      ContentNames::const_iterator iter1 = names1.begin();
      ContentNames::const_iterator iter2 = names2.begin();
      while (iter1 != names1.end()) {
        EXPECT_EQ(*iter1++, *iter2++);
      }
    }

    // transport info
    const TransportInfos transports1 = desc1.transport_infos();
    const TransportInfos transports2 = desc2.transport_infos();
    EXPECT_EQ(transports1.size(), transports2.size());
    if (transports1.size() != transports2.size()) {
      ADD_FAILURE();
      return;
    }
    for (size_t i = 0; i < transports1.size(); ++i) {
      const TransportInfo transport1 = transports1.at(i);
      const TransportInfo transport2 = transports2.at(i);
      EXPECT_EQ(transport1.content_name, transport2.content_name);
      EXPECT_EQ(transport1.description.ice_ufrag,
                transport2.description.ice_ufrag);
      EXPECT_EQ(transport1.description.ice_pwd, transport2.description.ice_pwd);
      EXPECT_EQ(transport1.description.ice_mode,
                transport2.description.ice_mode);
      if (transport1.description.identity_fingerprint) {
        if (!transport2.description.identity_fingerprint) {
          ADD_FAILURE() << "transport[" << i
                        << "]: left transport has fingerprint, right transport "
                           "does not have it";
        } else {
          EXPECT_EQ(*transport1.description.identity_fingerprint,
                    *transport2.description.identity_fingerprint);
        }
      } else {
        EXPECT_EQ(transport1.description.identity_fingerprint,
                  transport2.description.identity_fingerprint);
      }
      EXPECT_EQ(transport1.description.transport_options,
                transport2.description.transport_options);
    }

    // global attributes
    EXPECT_EQ(desc1.msid_signaling(), desc2.msid_signaling());
    EXPECT_EQ(desc1.extmap_allow_mixed(), desc2.extmap_allow_mixed());
  }

  bool CompareSessionDescription(const SessionDescriptionInterface& desc1,
                                 const SessionDescriptionInterface& desc2) {
    EXPECT_EQ(desc1.session_id(), desc2.session_id());
    EXPECT_EQ(desc1.session_version(), desc2.session_version());
    CompareSessionDescription(*desc1.description(), *desc2.description());
    if (desc1.number_of_mediasections() != desc2.number_of_mediasections())
      return false;
    for (size_t i = 0; i < desc1.number_of_mediasections(); ++i) {
      const IceCandidateCollection* cc1 = desc1.candidates(i);
      const IceCandidateCollection* cc2 = desc2.candidates(i);
      if (cc1->count() != cc2->count()) {
        EXPECT_EQ(cc1->count(), cc2->count());
        return false;
      }
      for (size_t j = 0; j < cc1->count(); ++j) {
        const IceCandidate* c1 = cc1->at(j);
        const IceCandidate* c2 = cc2->at(j);
        EXPECT_EQ(c1->sdp_mid(), c2->sdp_mid());
        EXPECT_EQ(c1->sdp_mline_index(), c2->sdp_mline_index());
        EXPECT_TRUE(c1->candidate().IsEquivalent(c2->candidate()));
      }
    }
    return true;
  }
  // Convenience helpers while migrating over to unique_ptr<>.
  bool CompareSessionDescription(
      const std::unique_ptr<SessionDescriptionInterface>& desc1,
      const SessionDescriptionInterface& desc2) {
    if (!desc1)
      return false;
    return CompareSessionDescription(*desc1, desc2);
  }
  bool CompareSessionDescription(
      const SessionDescriptionInterface& desc1,
      const std::unique_ptr<SessionDescriptionInterface>& desc2) {
    if (!desc2)
      return false;
    return CompareSessionDescription(desc1, *desc2);
  }
  bool CompareSessionDescription(
      const std::unique_ptr<SessionDescriptionInterface>& desc1,
      const std::unique_ptr<SessionDescriptionInterface>& desc2) {
    if (!desc1 || !desc2)
      return false;  // If both are null, then that's likely programmer error.
    return CompareSessionDescription(*desc1, *desc2);
  }

  // Calls `CompareSessionDescription()` to compare `sd` against the inner
  // `desc_` description along with the default set of candidates.
  bool MatchesCurrentDescription(
      const std::unique_ptr<SessionDescriptionInterface>& sd) {
    return CompareSessionDescription(NewSessionDescriptionWithCandidates(), sd);
  }

  // Calls `CompareSessionDescription()` to compare `sd` against the inner
  // `desc_` description without the default set of candidates.
  bool MatchesCurrentDescriptionNoCandidates(
      const std::unique_ptr<SessionDescriptionInterface>& sd) {
    return CompareSessionDescription(NewSessionDescriptionNoCandidates(), sd);
  }

  // Disable the ice-ufrag and ice-pwd in given `sdp` message by replacing
  // them with invalid keywords so that the parser will just ignore them.
  bool RemoveCandidateUfragPwd(std::string* sdp) {
    absl::StrReplaceAll(
        {{"a=ice-ufrag", "a=xice-ufrag"}, {"a=ice-pwd", "a=xice-pwd"}}, sdp);
    return true;
  }

  // Update the candidates in `jdesc` to use the given `ufrag` and `pwd`.
  bool UpdateCandidateUfragPwd(SessionDescriptionInterface* jdesc,
                               int mline_index,
                               const std::string& ufrag,
                               const std::string& pwd) {
    std::string content_name;
    if (mline_index == 0) {
      content_name = kAudioContentName;
    } else if (mline_index == 1) {
      content_name = kVideoContentName;
    } else {
      RTC_DCHECK_NOTREACHED();
    }
    TransportInfo transport_info(content_name,
                                 MakeTransportDescription(ufrag, pwd));
    SessionDescription* desc =
        const_cast<SessionDescription*>(jdesc->description());
    desc->RemoveTransportInfoByName(content_name);
    desc->AddTransportInfo(transport_info);
    for (size_t i = 0; i < jdesc_->number_of_mediasections(); ++i) {
      const IceCandidateCollection* cc = jdesc_->candidates(i);
      for (size_t j = 0; j < cc->count(); ++j) {
        if (cc->at(j)->sdp_mline_index() == mline_index) {
          const_cast<Candidate&>(cc->at(j)->candidate()).set_username(ufrag);
          const_cast<Candidate&>(cc->at(j)->candidate()).set_password(pwd);
        }
      }
    }
    return true;
  }

  void AddIceOptions(const std::string& content_name,
                     const std::vector<std::string>& transport_options) {
    ASSERT_TRUE(desc_.GetTransportInfoByName(content_name) != nullptr);
    TransportInfo transport_info =
        *(desc_.GetTransportInfoByName(content_name));
    desc_.RemoveTransportInfoByName(content_name);
    transport_info.description.transport_options = transport_options;
    desc_.AddTransportInfo(transport_info);
  }

  void SetIceUfragPwd(const std::string& content_name,
                      const std::string& ice_ufrag,
                      const std::string& ice_pwd) {
    ASSERT_TRUE(desc_.GetTransportInfoByName(content_name) != nullptr);
    TransportInfo transport_info =
        *(desc_.GetTransportInfoByName(content_name));
    desc_.RemoveTransportInfoByName(content_name);
    transport_info.description.ice_ufrag = ice_ufrag;
    transport_info.description.ice_pwd = ice_pwd;
    desc_.AddTransportInfo(transport_info);
  }

  void AddExtmap(bool encrypted) {
    audio_desc_ = new AudioContentDescription(*audio_desc_);
    video_desc_ = new VideoContentDescription(*video_desc_);
    audio_desc_->AddRtpHeaderExtension(
        RtpExtension(kExtmapUri, kExtmapId, encrypted));
    video_desc_->AddRtpHeaderExtension(
        RtpExtension(kExtmapUri, kExtmapId, encrypted));
    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp,
                     absl::WrapUnique(audio_desc_));
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp,
                     absl::WrapUnique(video_desc_));
  }

  // Removes everything in StreamParams from the session description that is
  // used for a=ssrc lines.
  void RemoveSsrcSignalingFromStreamParams() {
    for (ContentInfo& content_info : jdesc_->description()->contents()) {
      // With Unified Plan there should be one StreamParams per m= section.
      StreamParams& stream =
          content_info.media_description()->mutable_streams()[0];
      stream.ssrcs.clear();
      stream.ssrc_groups.clear();
      stream.cname.clear();
    }
  }

  // Removes all a=ssrc lines from the SDP string, except for the
  // "a=ssrc:... cname:..." lines.
  void RemoveSsrcMsidLinesFromSdpString(std::string* sdp_string) {
    const char kAttributeSsrc[] = "a=ssrc";
    const char kAttributeCname[] = "cname";
    size_t ssrc_line_pos = sdp_string->find(kAttributeSsrc);
    while (ssrc_line_pos != std::string::npos) {
      size_t beg_line_pos = sdp_string->rfind('\n', ssrc_line_pos);
      size_t end_line_pos = sdp_string->find('\n', ssrc_line_pos);
      size_t cname_pos = sdp_string->find(kAttributeCname, ssrc_line_pos);
      if (cname_pos == std::string::npos || cname_pos > end_line_pos) {
        // Only erase a=ssrc lines that don't contain "cname".
        sdp_string->erase(beg_line_pos, end_line_pos - beg_line_pos);
        ssrc_line_pos = sdp_string->find(kAttributeSsrc, beg_line_pos);
      } else {
        // Skip the "a=ssrc:... cname" line and find the next "a=ssrc" line.
        ssrc_line_pos = sdp_string->find(kAttributeSsrc, end_line_pos);
      }
    }
  }

  // Removes all a=ssrc lines from the SDP string.
  void RemoveSsrcLinesFromSdpString(std::string* sdp_string) {
    const char kAttributeSsrc[] = "a=ssrc";
    while (sdp_string->find(kAttributeSsrc) != std::string::npos) {
      size_t pos_ssrc_attribute = sdp_string->find(kAttributeSsrc);
      size_t beg_line_pos = sdp_string->rfind('\n', pos_ssrc_attribute);
      size_t end_line_pos = sdp_string->find('\n', pos_ssrc_attribute);
      sdp_string->erase(beg_line_pos, end_line_pos - beg_line_pos);
    }
  }

  bool TestSerializeDirection(RtpTransceiverDirection direction) {
    audio_desc_->set_direction(direction);
    video_desc_->set_direction(direction);
    std::string new_sdp = kSdpFullString;
    ReplaceDirection(direction, &new_sdp);
    std::string message = SerializeCurrentDescription();
    EXPECT_EQ(new_sdp, message);
    return new_sdp == message;
  }

  bool TestSerializeRejected(bool audio_rejected, bool video_rejected) {
    audio_desc_ = new AudioContentDescription(*audio_desc_);
    video_desc_ = new VideoContentDescription(*video_desc_);

    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_rejected,
                     absl::WrapUnique(audio_desc_));
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp, video_rejected,
                     absl::WrapUnique(video_desc_));
    SetIceUfragPwd(kAudioContentName, audio_rejected ? "" : kUfragVoice,
                   audio_rejected ? "" : kPwdVoice);
    SetIceUfragPwd(kVideoContentName, video_rejected ? "" : kUfragVideo,
                   video_rejected ? "" : kPwdVideo);

    std::string new_sdp = kSdpString;
    ReplaceRejected(audio_rejected, video_rejected, &new_sdp);
    EXPECT_EQ(new_sdp, SdpSerialize(MakeDescriptionWithoutCandidates()));
    return true;
  }

  void AddSctpDataChannel(bool use_sctpmap) {
    std::unique_ptr<SctpDataContentDescription> data(
        new SctpDataContentDescription());
    sctp_desc_ = data.get();
    sctp_desc_->set_use_sctpmap(use_sctpmap);
    sctp_desc_->set_protocol(kMediaProtocolUdpDtlsSctp);
    sctp_desc_->set_port(kDefaultSctpPort);
    desc_.AddContent(kDataContentName, MediaProtocolType::kSctp,
                     std::move(data));
    desc_.AddTransportInfo(TransportInfo(
        kDataContentName, MakeTransportDescription(kUfragData, kPwdData)));
  }

  bool TestDeserializeDirection(RtpTransceiverDirection direction) {
    std::string new_sdp = kSdpFullString;
    ReplaceDirection(direction, &new_sdp);
    std::unique_ptr<SessionDescriptionInterface> new_jdesc =
        SdpDeserialize(new_sdp);
    EXPECT_THAT(new_jdesc, NotNull());
    if (!new_jdesc)
      return false;

    audio_desc_->set_direction(direction);
    video_desc_->set_direction(direction);
    return MatchesCurrentDescription(new_jdesc);
  }

  bool TestDeserializeRejected(bool audio_rejected, bool video_rejected) {
    std::string new_sdp = kSdpString;
    ReplaceRejected(audio_rejected, video_rejected, &new_sdp);
    std::unique_ptr<SessionDescriptionInterface> new_jdesc =
        SdpDeserialize(new_sdp);
    EXPECT_THAT(new_jdesc, NotNull());
    if (!new_jdesc)
      return false;

    audio_desc_ = new AudioContentDescription(*audio_desc_);
    video_desc_ = new VideoContentDescription(*video_desc_);
    desc_.RemoveContentByName(kAudioContentName);
    desc_.RemoveContentByName(kVideoContentName);
    desc_.AddContent(kAudioContentName, MediaProtocolType::kRtp, audio_rejected,
                     absl::WrapUnique(audio_desc_));
    desc_.AddContent(kVideoContentName, MediaProtocolType::kRtp, video_rejected,
                     absl::WrapUnique(video_desc_));
    SetIceUfragPwd(kAudioContentName, audio_rejected ? "" : kUfragVoice,
                   audio_rejected ? "" : kPwdVoice);
    SetIceUfragPwd(kVideoContentName, video_rejected ? "" : kUfragVideo,
                   video_rejected ? "" : kPwdVideo);
    EXPECT_TRUE(MatchesCurrentDescriptionNoCandidates(new_jdesc));
    return true;
  }

  void TestDeserializeExtmap(bool session_level,
                             bool media_level,
                             bool encrypted) {
    AddExtmap(encrypted);
    std::unique_ptr<SessionDescriptionInterface> new_jdesc =
        CreateSessionDescription(SdpType::kOffer, jdesc_->session_id(),
                                 jdesc_->session_version(), desc_.Clone());
    ASSERT_THAT(new_jdesc, NotNull());

    std::string sdp_with_extmap = kSdpString;
    if (session_level) {
      InjectAfter(kSessionTime,
                  encrypted ? kExtmapWithDirectionAndAttributeEncrypted
                            : kExtmapWithDirectionAndAttribute,
                  &sdp_with_extmap);
    }
    if (media_level) {
      InjectAfter(kAttributeIcePwdVoice,
                  encrypted ? kExtmapWithDirectionAndAttributeEncrypted
                            : kExtmapWithDirectionAndAttribute,
                  &sdp_with_extmap);
      InjectAfter(kAttributeIcePwdVideo,
                  encrypted ? kExtmapWithDirectionAndAttributeEncrypted
                            : kExtmapWithDirectionAndAttribute,
                  &sdp_with_extmap);
    }
    // The extmap can't be present at the same time in both session level and
    // media level.
    if (session_level && media_level) {
      SdpParseError error;
      EXPECT_THAT(SdpDeserialize(sdp_with_extmap, &error), IsNull());
      EXPECT_NE(std::string::npos, error.description.find("a=extmap"));
    } else {
      EXPECT_TRUE(CompareSessionDescription(SdpDeserialize(sdp_with_extmap),
                                            new_jdesc));
    }
  }

  void VerifyCodecParameter(const CodecParameterMap& params,
                            const std::string& name,
                            int expected_value) {
    CodecParameterMap::const_iterator found = params.find(name);
    ASSERT_TRUE(found != params.end());
    EXPECT_EQ(found->second, absl::StrCat(expected_value));
  }

  void TestDeserializeCodecParams(
      const CodecParams& params,
      std::unique_ptr<SessionDescriptionInterface>& jdesc_output) {
    std::string sdp =
        "v=0\r\n"
        "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        // Include semantics for WebRTC Media Streams since it is supported by
        // this parser, and will be added to the SDP when serializing a session
        // description.
        "a=msid-semantic: WMS\r\n"
        // Pl type 111 preferred.
        "m=audio 9 RTP/SAVPF 111 104 103 105\r\n"
        // Pltype 111 listed before 103 and 104 in the map.
        "a=rtpmap:111 opus/48000/2\r\n"
        // Pltype 103 listed before 104.
        "a=rtpmap:103 ISAC/16000\r\n"
        "a=rtpmap:104 ISAC/32000\r\n"
        "a=rtpmap:105 telephone-event/8000\r\n"
        "a=fmtp:105 0-15,66,70\r\n"
        "a=fmtp:111 ";
    StringBuilder sb;
    sb << "minptime=" << params.min_ptime << "; stereo=" << params.stereo
       << "; sprop-stereo=" << params.sprop_stereo
       << "; useinbandfec=" << params.useinband
       << "; maxaveragebitrate=" << params.maxaveragebitrate
       << "\r\n"
          "a=ptime:"
       << params.ptime
       << "\r\n"
          "a=maxptime:"
       << params.max_ptime << "\r\n";
    sdp += sb.Release();

    // Pl type 100 preferred.
    sdp +=
        "m=video 9 RTP/SAVPF 99 95 96\r\n"
        "a=rtpmap:96 VP9/90000\r\n"  // out-of-order wrt the m= line.
        "a=rtpmap:99 VP8/90000\r\n"
        "a=rtpmap:95 RTX/90000\r\n"
        "a=fmtp:95 apt=99;\r\n";

    // Deserialize
    SdpParseError error;
    jdesc_output = SdpDeserialize(sdp, &error);
    ASSERT_THAT(jdesc_output, NotNull());

    const AudioContentDescription* acd =
        GetFirstAudioContentDescription(jdesc_output->description());
    ASSERT_THAT(acd, NotNull());
    ASSERT_FALSE(acd->codecs().empty());
    Codec opus = acd->codecs()[0];
    EXPECT_EQ("opus", opus.name);
    EXPECT_EQ(111, opus.id);
    VerifyCodecParameter(opus.params, "minptime", params.min_ptime);
    VerifyCodecParameter(opus.params, "stereo", params.stereo);
    VerifyCodecParameter(opus.params, "sprop-stereo", params.sprop_stereo);
    VerifyCodecParameter(opus.params, "useinbandfec", params.useinband);
    VerifyCodecParameter(opus.params, "maxaveragebitrate",
                         params.maxaveragebitrate);
    for (const auto& codec : acd->codecs()) {
      VerifyCodecParameter(codec.params, "ptime", params.ptime);
      VerifyCodecParameter(codec.params, "maxptime", params.max_ptime);
    }

    Codec dtmf = acd->codecs()[3];
    EXPECT_EQ("telephone-event", dtmf.name);
    EXPECT_EQ(105, dtmf.id);
    EXPECT_EQ(3u,
              dtmf.params.size());  // ptime and max_ptime count as parameters.
    EXPECT_EQ(dtmf.params.begin()->first, "");
    EXPECT_EQ(dtmf.params.begin()->second, "0-15,66,70");

    const VideoContentDescription* vcd =
        GetFirstVideoContentDescription(jdesc_output->description());
    ASSERT_THAT(vcd, NotNull());
    ASSERT_FALSE(vcd->codecs().empty());
    Codec vp8 = vcd->codecs()[0];
    EXPECT_EQ("VP8", vp8.name);
    EXPECT_EQ(99, vp8.id);
    Codec rtx = vcd->codecs()[1];
    EXPECT_EQ("RTX", rtx.name);
    EXPECT_EQ(95, rtx.id);
    VerifyCodecParameter(rtx.params, "apt", vp8.id);
    // VP9 is listed last in the m= line so should come after VP8 and RTX.
    Codec vp9 = vcd->codecs()[2];
    EXPECT_EQ("VP9", vp9.name);
    EXPECT_EQ(96, vp9.id);
  }

  void TestDeserializeRtcpFb(
      std::unique_ptr<SessionDescriptionInterface>& jdesc_output,
      bool use_wildcard,
      bool use_ccfb) {
    std::string sdp_session_and_audio =
        "v=0\r\n"
        "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        // Include semantics for WebRTC Media Streams since it is supported by
        // this parser, and will be added to the SDP when serializing a session
        // description.
        "a=msid-semantic: WMS\r\n"
        "m=audio 9 RTP/SAVPF 111\r\n"
        "a=rtpmap:111 opus/48000/2\r\n";
    std::string sdp_video =
        "m=video 3457 RTP/SAVPF 101\r\n"
        "a=rtpmap:101 VP8/90000\r\n"
        "a=rtcp-fb:101 goog-lntf\r\n"
        "a=rtcp-fb:101 nack\r\n"
        "a=rtcp-fb:101 nack pli\r\n"
        "a=rtcp-fb:101 goog-remb\r\n";
    StringBuilder sb;
    sb << sdp_session_and_audio;
    sb << "a=rtcp-fb:" << (use_wildcard ? "*" : "111") << " nack\r\n";
    if (use_ccfb) {
      sb << "a=rtcp-fb:" << (use_wildcard ? "*" : "111") << " ack ccfb\r\n";
    }
    sb << sdp_video;
    sb << "a=rtcp-fb:" << (use_wildcard ? "*" : "101") << " ccm fir\r\n";
    if (use_ccfb) {
      sb << "a=rtcp-fb:" << (use_wildcard ? "*" : "101") << " ack ccfb\r\n";
    }
    std::string sdp = sb.Release();
    // Deserialize
    SdpParseError error;
    jdesc_output = SdpDeserialize(sdp, &error);
    ASSERT_THAT(jdesc_output, NotNull());
    const AudioContentDescription* acd =
        GetFirstAudioContentDescription(jdesc_output->description());
    ASSERT_THAT(acd, NotNull());
    ASSERT_FALSE(acd->codecs().empty());
    Codec opus = acd->codecs()[0];
    EXPECT_EQ(111, opus.id);
    EXPECT_TRUE(opus.HasFeedbackParam(
        FeedbackParam(kRtcpFbParamNack, kParamValueEmpty)));

    const VideoContentDescription* vcd =
        GetFirstVideoContentDescription(jdesc_output->description());
    ASSERT_THAT(vcd, NotNull());
    ASSERT_FALSE(vcd->codecs().empty());
    Codec vp8 = vcd->codecs()[0];
    EXPECT_EQ(vp8.name, "VP8");
    EXPECT_EQ(101, vp8.id);
    EXPECT_TRUE(vp8.HasFeedbackParam(
        FeedbackParam(kRtcpFbParamLntf, kParamValueEmpty)));
    EXPECT_TRUE(vp8.HasFeedbackParam(
        FeedbackParam(kRtcpFbParamNack, kParamValueEmpty)));
    EXPECT_TRUE(vp8.HasFeedbackParam(
        FeedbackParam(kRtcpFbParamNack, kRtcpFbNackParamPli)));
    EXPECT_TRUE(vp8.HasFeedbackParam(
        FeedbackParam(kRtcpFbParamRemb, kParamValueEmpty)));
    EXPECT_TRUE(vp8.HasFeedbackParam(
        FeedbackParam(kRtcpFbParamCcm, kRtcpFbCcmParamFir)));
    MediaContentDescription* video_media =
        GetFirstMediaContent(jdesc_output->description(), MediaType::VIDEO)
            ->media_description();
    MediaContentDescription* audio_media =
        GetFirstMediaContent(jdesc_output->description(), MediaType::AUDIO)
            ->media_description();
    if (use_ccfb) {
      EXPECT_TRUE(video_media->rtcp_fb_ack_ccfb())
          << "Wildcard: " << use_wildcard;
      EXPECT_TRUE(audio_media->rtcp_fb_ack_ccfb())
          << "Wildcard: " << use_wildcard;
    } else {
      EXPECT_FALSE(video_media->rtcp_fb_ack_ccfb());
      EXPECT_FALSE(audio_media->rtcp_fb_ack_ccfb());
    }
  }

  // Two SDP messages can mean the same thing but be different strings, e.g.
  // some of the lines can be serialized in different order.
  // However, a deserialized description can be compared field by field and has
  // no order. If deserializer has already been tested, serializing then
  // deserializing and comparing session descriptions will test
  // the serializer sufficiently.
  void TestSerialize(
      const std::unique_ptr<SessionDescriptionInterface>& jdesc) {
    ASSERT_THAT(jdesc, NotNull());
    std::string message = SdpSerialize(jdesc);
    std::unique_ptr<SessionDescriptionInterface> jdesc_output_des =
        SdpDeserialize(message);
    ASSERT_THAT(jdesc_output_des, NotNull());
    EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output_des));
  }

  // Returns a new session description with a clone of the inner
  // SessionDescription but no candidates. The 'connection address' field
  // for the audio and video descriptions, previously set from the candidates,
  // will be reset on both the returned object and the inner.
  std::unique_ptr<SessionDescriptionInterface>
  MakeDescriptionWithoutCandidates() {
    audio_desc_->set_connection_address(SocketAddress("0.0.0.0", 9));
    video_desc_->set_connection_address(SocketAddress("0.0.0.0", 9));
    return CreateSessionDescription(kDummyType, kSessionId, kSessionVersion,
                                    desc_.Clone());
  }

  // Returns an SDP string generated from the current state of `desc_`
  // together with the default set of candidates.
  std::string SerializeCurrentDescription() {
    return SdpSerialize(NewSessionDescriptionWithCandidates());
  }

 protected:
  SessionDescription desc_;
  AudioContentDescription* audio_desc_;
  VideoContentDescription* video_desc_;
  SctpDataContentDescription* sctp_desc_;
  std::vector<Candidate> candidates_;
  std::unique_ptr<IceCandidate> jcandidate_;
  std::unique_ptr<SessionDescriptionInterface> jdesc_;
};

void TestMismatch(const std::string& string1, const std::string& string2) {
  int position = 0;
  for (size_t i = 0; i < string1.length() && i < string2.length(); ++i) {
    if (string1.c_str()[i] != string2.c_str()[i]) {
      position = static_cast<int>(i);
      break;
    }
  }
  EXPECT_EQ(0, position) << "Strings mismatch at the " << position
                         << " character\n"
                            " 1: "
                         << string1.substr(position, 20)
                         << "\n"
                            " 2: "
                         << string2.substr(position, 20) << "\n";
}

TEST_F(WebRtcSdpTest, SerializeSessionDescription) {
  // SessionDescription with desc and candidates.
  std::string message = SdpSerialize(jdesc_);
  TestMismatch(std::string(kSdpFullString), message);
}

// This basically tests if SdpSerialize() checks the description() property.
// kRollback is the only type that's allowed to have a null description field
// so that's what we'll use.
TEST_F(WebRtcSdpTest, SerializeSessionDescriptionEmpty) {
  std::unique_ptr<SessionDescriptionInterface> jdesc_empty =
      CreateRollbackSessionDescription(kSessionId, kSessionVersion);
  ASSERT_THAT(jdesc_empty, NotNull());
  EXPECT_THAT(jdesc_empty->description(), IsNull());
  EXPECT_EQ("", SdpSerialize(jdesc_empty));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithoutCandidates) {
  // Session description with desc but without candidates.
  EXPECT_EQ(SdpSerialize(MakeDescriptionWithoutCandidates()), kSdpString);
}

// Tests that each m= section is serialized with its own transport info even
// when the transport infos are not stored in the same order as the contents.
TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithTransportInfosOutOfOrder) {
  TransportInfo audio_transport_info =
      *desc_.GetTransportInfoByName(kAudioContentName);
  ASSERT_TRUE(desc_.RemoveTransportInfoByName(kAudioContentName));
  desc_.AddTransportInfo(audio_transport_info);
  ASSERT_EQ(desc_.transport_infos()[0].content_name, kVideoContentName);
  TestMismatch(std::string(kSdpFullString),
               SdpSerialize(NewSessionDescriptionWithCandidates()));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithBundles) {
  ContentGroup group1(GROUP_TYPE_BUNDLE);
  group1.AddContentName(kAudioContentName);
  group1.AddContentName(kVideoContentName);
  desc_.AddGroup(group1);
  ContentGroup group2(GROUP_TYPE_BUNDLE);
  group2.AddContentName(kAudioContentName2);
  desc_.AddGroup(group2);
  std::string message = SerializeCurrentDescription();
  std::string sdp_with_bundle = kSdpFullString;
  InjectAfter(kSessionTime,
              "a=group:BUNDLE audio_content_name video_content_name\r\n"
              "a=group:BUNDLE audio_content_name_2\r\n",
              &sdp_with_bundle);
  EXPECT_EQ(sdp_with_bundle, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithBandwidth) {
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);
  vcd->set_bandwidth(100 * 1000 + 755);  // Integer division will drop the 755.
  vcd->set_bandwidth_type("AS");
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);
  acd->set_bandwidth(555);
  acd->set_bandwidth_type("TIAS");
  std::string message = SerializeCurrentDescription();
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("c=IN IP4 74.125.224.39\r\n", "b=AS:100\r\n",
              &sdp_with_bandwidth);
  InjectAfter("c=IN IP4 74.125.127.126\r\n", "b=TIAS:555\r\n",
              &sdp_with_bandwidth);
  EXPECT_EQ(sdp_with_bandwidth, message);
}

// Should default to b=AS if bandwidth_type isn't set.
TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithMissingBandwidthType) {
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);
  vcd->set_bandwidth(100 * 1000);
  std::string message = SerializeCurrentDescription();
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("c=IN IP4 74.125.224.39\r\n", "b=AS:100\r\n",
              &sdp_with_bandwidth);
  EXPECT_EQ(sdp_with_bandwidth, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithIceOptions) {
  std::vector<std::string> transport_options;
  transport_options.push_back(kIceOption1);
  transport_options.push_back(kIceOption3);
  AddIceOptions(kAudioContentName, transport_options);
  transport_options.clear();
  transport_options.push_back(kIceOption2);
  transport_options.push_back(kIceOption3);
  AddIceOptions(kVideoContentName, transport_options);
  std::string message = SerializeCurrentDescription();
  std::string sdp_with_ice_options = kSdpFullString;
  InjectAfter(kAttributeIcePwdVoice, "a=ice-options:iceoption1 iceoption3\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVideo, "a=ice-options:iceoption2 iceoption3\r\n",
              &sdp_with_ice_options);
  EXPECT_EQ(sdp_with_ice_options, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithRecvOnlyContent) {
  EXPECT_TRUE(TestSerializeDirection(RtpTransceiverDirection::kRecvOnly));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithSendOnlyContent) {
  EXPECT_TRUE(TestSerializeDirection(RtpTransceiverDirection::kSendOnly));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithInactiveContent) {
  EXPECT_TRUE(TestSerializeDirection(RtpTransceiverDirection::kInactive));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithAudioRejected) {
  EXPECT_TRUE(TestSerializeRejected(true, false));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithVideoRejected) {
  EXPECT_TRUE(TestSerializeRejected(false, true));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithAudioVideoRejected) {
  EXPECT_TRUE(TestSerializeRejected(true, true));
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithSctpDataChannel) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());

  std::string expected_sdp = kSdpString;
  expected_sdp.append(kSdpSctpDataChannelString);
  EXPECT_EQ(message, expected_sdp);
}

std::unique_ptr<SessionDescriptionInterface> MutateSctpPort(
    const SessionDescriptionInterface* jdesc,
    const SessionDescription& desc,
    int port) {
  EXPECT_THAT(jdesc, NotNull());
  if (!jdesc)
    return nullptr;
  // Take our pre-built session description and change the SCTP port.
  std::unique_ptr<SessionDescription> mutant = desc.Clone();
  SctpDataContentDescription* dcdesc =
      mutant->GetContentDescriptionByName(kDataContentName)->as_sctp();
  dcdesc->set_port(port);
  return CreateSessionDescription(jdesc->GetType(), jdesc->session_id(),
                                  jdesc->session_version(), std::move(mutant));
}

TEST_F(WebRtcSdpTest, SerializeWithSctpDataChannelAndNewPort) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  const int kNewPort = 1234;
  std::unique_ptr<SessionDescriptionInterface> jsep_desc =
      MutateSctpPort(MakeDescriptionWithoutCandidates().get(), desc_, kNewPort);
  ASSERT_THAT(jsep_desc, NotNull());
  std::string message = SdpSerialize(jsep_desc);

  std::string expected_sdp = kSdpString;
  expected_sdp.append(kSdpSctpDataChannelString);

  absl::StrReplaceAll(
      {{absl::StrCat(kDefaultSctpPort), absl::StrCat(kNewPort)}},
      &expected_sdp);

  EXPECT_EQ(expected_sdp, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithExtmapAllowMixed) {
  jdesc_->description()->set_extmap_allow_mixed(true);
  TestSerialize(jdesc_);
}

TEST_F(WebRtcSdpTest, SerializeMediaContentDescriptionWithExtmapAllowMixed) {
  MediaContentDescription* video_desc =
      jdesc_->description()->GetContentDescriptionByName(kVideoContentName);
  ASSERT_THAT(video_desc, NotNull());
  MediaContentDescription* audio_desc =
      jdesc_->description()->GetContentDescriptionByName(kAudioContentName);
  ASSERT_THAT(audio_desc, NotNull());
  video_desc->set_extmap_allow_mixed_level(
      MediaContentDescription::AttributeLevel::kMedia);
  audio_desc->set_extmap_allow_mixed_level(
      MediaContentDescription::AttributeLevel::kMedia);
  TestSerialize(jdesc_);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithExtmap) {
  bool encrypted = false;
  AddExtmap(encrypted);
  std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());

  std::string sdp_with_extmap = kSdpString;
  InjectAfter("a=mid:audio_content_name\r\n", kExtmap, &sdp_with_extmap);
  InjectAfter("a=mid:video_content_name\r\n", kExtmap, &sdp_with_extmap);

  EXPECT_EQ(sdp_with_extmap, message);
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithExtmapEncrypted) {
  bool encrypted = true;
  AddExtmap(encrypted);
  TestSerialize(CreateSessionDescription(kDummyType, kSessionId,
                                         kSessionVersion, desc_.Clone()));
}

TEST_F(WebRtcSdpTest, SerializeCandidates) {
  std::string message = SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate), message);

  Candidate candidate_with_ufrag(candidates_.front());
  candidate_with_ufrag.set_username("ABC");
  jcandidate_.reset(new IceCandidate(std::string("audio_content_name"), 0,
                                     candidate_with_ufrag));
  message = SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate) + " ufrag ABC", message);

  Candidate candidate_with_network_info(candidates_.front());
  candidate_with_network_info.set_network_id(1);
  jcandidate_.reset(
      new IceCandidate(std::string("audio"), 0, candidate_with_network_info));
  message = SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate) + " network-id 1", message);
  candidate_with_network_info.set_network_cost(999);
  jcandidate_.reset(
      new IceCandidate(std::string("audio"), 0, candidate_with_network_info));
  message = SdpSerializeCandidate(*jcandidate_);
  EXPECT_EQ(std::string(kRawCandidate) + " network-id 1 network-cost 999",
            message);
}

TEST_F(WebRtcSdpTest, SerializeHostnameCandidate) {
  SocketAddress address("a.test", 1234);
  Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "udp", address,
                      kCandidatePriority, "", "", IceCandidateType::kHost,
                      kCandidateGeneration, kCandidateFoundation1);
  IceCandidate jcandidate(std::string("audio_content_name"), 0, candidate);
  std::string message = SdpSerializeCandidate(jcandidate);
  EXPECT_EQ(std::string(kRawHostnameCandidate), message);
}

TEST_F(WebRtcSdpTest, SerializeTcpCandidates) {
  Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                      SocketAddress("192.168.1.5", 9), kCandidatePriority, "",
                      "", IceCandidateType::kHost, kCandidateGeneration,
                      kCandidateFoundation1);
  candidate.set_tcptype(TCPTYPE_ACTIVE_STR);
  std::unique_ptr<IceCandidate> jcandidate(
      new IceCandidate(std::string("audio_content_name"), 0, candidate));

  std::string message = SdpSerializeCandidate(*jcandidate);
  EXPECT_EQ(std::string(kSdpTcpActiveCandidate), message);
}

// Test serializing a TCP candidate that came in with a missing tcptype. This
// shouldn't happen according to the spec, but our implementation has been
// accepting this for quite some time, treating it as a passive candidate.
//
// So, we should be able to at least convert such candidates to and from SDP.
// See: bugs.webrtc.org/11423
TEST_F(WebRtcSdpTest, ParseTcpCandidateWithoutTcptype) {
  std::string missing_tcptype =
      "candidate:a0+B/1 1 tcp 2130706432 192.168.1.5 9999 typ host";
  std::unique_ptr<IceCandidate> jcandidate = NewCandidate(missing_tcptype);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(std::string(TCPTYPE_PASSIVE_STR),
            jcandidate->candidate().tcptype());
}

TEST_F(WebRtcSdpTest, ParseSslTcpCandidate) {
  std::string ssltcp =
      "candidate:a0+B/1 1 ssltcp 2130706432 192.168.1.5 9999 typ host tcptype "
      "passive";
  std::unique_ptr<IceCandidate> jcandidate = NewCandidate(ssltcp);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(std::string("ssltcp"), jcandidate->candidate().protocol());
}

TEST_F(WebRtcSdpTest, SerializeSessionDescriptionWithH264) {
  Codec h264_codec = CreateVideoCodec("H264");
  // Id must be valid, but value doesn't matter.
  h264_codec.id = 123;
  h264_codec.SetParam("profile-level-id", "42e01f");
  h264_codec.SetParam("level-asymmetry-allowed", "1");
  h264_codec.SetParam("packetization-mode", "1");
  video_desc_->AddCodec(h264_codec);

  std::string message = SerializeCurrentDescription();
  size_t after_pt = message.find(" H264/90000");
  ASSERT_NE(after_pt, std::string::npos);
  size_t before_pt = message.rfind("a=rtpmap:", after_pt);
  ASSERT_NE(before_pt, std::string::npos);
  before_pt += strlen("a=rtpmap:");
  std::string pt = message.substr(before_pt, after_pt - before_pt);
  // TODO(hta): Check if payload type `pt` occurs in the m=video line.
  std::string to_find = "a=fmtp:" + pt + " ";
  size_t fmtp_pos = message.find(to_find);
  ASSERT_NE(std::string::npos, fmtp_pos) << "Failed to find " << to_find;
  size_t fmtp_endpos = message.find('\n', fmtp_pos);
  ASSERT_NE(std::string::npos, fmtp_endpos);
  std::string fmtp_value = message.substr(fmtp_pos, fmtp_endpos);
  EXPECT_NE(std::string::npos, fmtp_value.find("level-asymmetry-allowed=1"));
  EXPECT_NE(std::string::npos, fmtp_value.find("packetization-mode=1"));
  EXPECT_NE(std::string::npos, fmtp_value.find("profile-level-id=42e01f"));
  // Check that there are no spaces after semicolons.
  // https://bugs.webrtc.org/5793
  EXPECT_EQ(std::string::npos, fmtp_value.find("; "));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescription) {
  // Deserialize & Verify
  EXPECT_TRUE(
      CompareSessionDescription(jdesc_, SdpDeserialize(kSdpFullString)));
}

// Tests that duplicate a=candidate lines within one m= section only add one
// candidate to the candidate collection of that m= section.
TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithDuplicateCandidate) {
  std::string sdp = kSdpFullString;
  const std::string candidate_line =
      "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
      "generation 2\r\n";
  InjectAfter(candidate_line, candidate_line, &sdp);
  std::unique_ptr<SessionDescriptionInterface> jdesc = SdpDeserialize(sdp);
  ASSERT_THAT(jdesc, NotNull());
  ASSERT_THAT(jdesc->candidates(0), NotNull());
  EXPECT_EQ(jdesc->candidates(0)->count(), jdesc_->candidates(0)->count());
  EXPECT_TRUE(CompareSessionDescription(jdesc_, jdesc));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutMline) {
  const char kSdpWithoutMline[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=msid-semantic: WMS local_stream_1 local_stream_2\r\n";
  // Deserialize
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(kSdpWithoutMline);
  ASSERT_THAT(jdesc, NotNull());
  EXPECT_EQ(0u, jdesc->description()->contents().size());
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutCarriageReturn) {
  std::string sdp_without_carriage_return = kSdpFullString;
  Replace("\r\n", "\n", &sdp_without_carriage_return);
  // Deserialize & Verify
  EXPECT_TRUE(CompareSessionDescription(
      jdesc_, SdpDeserialize(sdp_without_carriage_return)));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutCandidates) {
  // SessionDescription with desc but without candidates.
  std::unique_ptr<SessionDescriptionInterface> jdesc_no_candidates =
      CreateSessionDescription(kDummyType, kSessionId, kSessionVersion,
                               desc_.Clone());
  ASSERT_THAT(jdesc_no_candidates, NotNull());
  EXPECT_TRUE(CompareSessionDescription(jdesc_no_candidates,
                                        SdpDeserialize(kSdpString)));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutRtpmap) {
  static const char kSdpNoRtpmapString[] =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 49232 RTP/AVP 0 18 103\r\n"
      // Codec that doesn't appear in the m= line will be ignored.
      "a=rtpmap:104 ISAC/32000\r\n"
      // The rtpmap line for static payload codec is optional.
      "a=rtpmap:18 G729/8000\r\n"
      "a=rtpmap:103 ISAC/16000\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(kSdpNoRtpmapString);
  ASSERT_THAT(jdesc, NotNull());
  AudioContentDescription* audio =
      GetFirstAudioContentDescription(jdesc->description());
  Codecs ref_codecs;
  // The codecs in the AudioContentDescription should be in the same order as
  // the payload types (<fmt>s) on the m= line.
  ref_codecs.push_back(CreateAudioCodec(0, "PCMU", 8000, 1));
  ref_codecs.push_back(CreateAudioCodec(18, "G729", 8000, 1));
  ref_codecs.push_back(CreateAudioCodec(103, "ISAC", 16000, 1));
  EXPECT_EQ(ref_codecs, audio->codecs());
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutRtpmapButWithFmtp) {
  static const char kSdpNoRtpmapString[] =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 49232 RTP/AVP 18 103\r\n"
      "a=fmtp:18 annexb=yes\r\n"
      "a=rtpmap:103 ISAC/16000\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(kSdpNoRtpmapString);
  ASSERT_THAT(jdesc, NotNull());
  AudioContentDescription* audio =
      GetFirstAudioContentDescription(jdesc->description());

  Codec g729 = audio->codecs()[0];
  EXPECT_EQ("G729", g729.name);
  EXPECT_EQ(8000, g729.clockrate);
  EXPECT_EQ(18, g729.id);
  CodecParameterMap::iterator found = g729.params.find("annexb");
  ASSERT_TRUE(found != g729.params.end());
  EXPECT_EQ(found->second, "yes");

  Codec isac = audio->codecs()[1];
  EXPECT_EQ("ISAC", isac.name);
  EXPECT_EQ(103, isac.id);
  EXPECT_EQ(16000, isac.clockrate);
}

// Ensure that we can deserialize SDP with a=fingerprint properly.
TEST_F(WebRtcSdpTest, DeserializeJsepSessionDescriptionWithFingerprint) {
  std::string sdp_with_fingerprint = kSdpString;
  InjectAfter(kAttributeIcePwdVoice, kFingerprint, &sdp_with_fingerprint);
  InjectAfter(kAttributeIcePwdVideo, kFingerprint, &sdp_with_fingerprint);
  EXPECT_TRUE(MatchesCurrentDescriptionNoCandidates(
      SdpDeserialize(sdp_with_fingerprint)));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithBundle) {
  std::string sdp_with_bundle = kSdpFullString;
  InjectAfter(kSessionTime,
              "a=group:BUNDLE audio_content_name video_content_name\r\n",
              &sdp_with_bundle);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_bundle =
      SdpDeserialize(sdp_with_bundle);
  ASSERT_THAT(jdesc_with_bundle, NotNull());
  ContentGroup group(GROUP_TYPE_BUNDLE);
  group.AddContentName(kAudioContentName);
  group.AddContentName(kVideoContentName);
  desc_.AddGroup(group);
  EXPECT_TRUE(MatchesCurrentDescription(jdesc_with_bundle));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithBandwidth) {
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n", "b=AS:100\r\n",
              &sdp_with_bandwidth);
  InjectAfter("a=mid:audio_content_name\r\na=sendrecv\r\n", "b=AS:50\r\n",
              &sdp_with_bandwidth);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_bandwidth =
      SdpDeserialize(sdp_with_bandwidth);
  ASSERT_THAT(jdesc_with_bandwidth, NotNull());
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);
  vcd->set_bandwidth(100 * 1000);
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);
  acd->set_bandwidth(50 * 1000);
  EXPECT_TRUE(MatchesCurrentDescription(jdesc_with_bandwidth));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithTiasBandwidth) {
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n", "b=TIAS:100000\r\n",
              &sdp_with_bandwidth);
  InjectAfter("a=mid:audio_content_name\r\na=sendrecv\r\n", "b=TIAS:50000\r\n",
              &sdp_with_bandwidth);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_bandwidth =
      SdpDeserialize(sdp_with_bandwidth);
  ASSERT_THAT(jdesc_with_bandwidth, NotNull());
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);
  vcd->set_bandwidth(100 * 1000);
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);
  acd->set_bandwidth(50 * 1000);
  EXPECT_TRUE(MatchesCurrentDescription(jdesc_with_bandwidth));
}

TEST_F(WebRtcSdpTest,
       DeserializeSessionDescriptionWithUnknownBandwidthModifier) {
  std::string sdp_with_bandwidth = kSdpFullString;
  InjectAfter("a=mid:video_content_name\r\na=sendrecv\r\n",
              "b=unknown:100000\r\n", &sdp_with_bandwidth);
  InjectAfter("a=mid:audio_content_name\r\na=sendrecv\r\n",
              "b=unknown:50000\r\n", &sdp_with_bandwidth);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_bandwidth =
      SdpDeserialize(sdp_with_bandwidth);
  ASSERT_THAT(jdesc_with_bandwidth, NotNull());
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);
  vcd->set_bandwidth(-1);
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);
  acd->set_bandwidth(-1);
  EXPECT_TRUE(MatchesCurrentDescription(jdesc_with_bandwidth));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithIceOptions) {
  std::string sdp_with_ice_options = kSdpFullString;
  InjectAfter(kSessionTime, "a=ice-options:iceoption3\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVoice, "a=ice-options:iceoption1\r\n",
              &sdp_with_ice_options);
  InjectAfter(kAttributeIcePwdVideo, "a=ice-options:iceoption2\r\n",
              &sdp_with_ice_options);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_ice_options =
      SdpDeserialize(sdp_with_ice_options);
  ASSERT_THAT(jdesc_with_ice_options, NotNull());
  std::vector<std::string> transport_options;
  transport_options.push_back(kIceOption3);
  transport_options.push_back(kIceOption1);
  AddIceOptions(kAudioContentName, transport_options);
  transport_options.clear();
  transport_options.push_back(kIceOption3);
  transport_options.push_back(kIceOption2);
  AddIceOptions(kVideoContentName, transport_options);
  EXPECT_TRUE(MatchesCurrentDescription(jdesc_with_ice_options));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithUfragPwd) {
  // Remove the original ice-ufrag and ice-pwd
  std::string sdp_with_ufrag_pwd = kSdpFullString;
  EXPECT_TRUE(RemoveCandidateUfragPwd(&sdp_with_ufrag_pwd));
  // Add session level ufrag and pwd
  InjectAfter(kSessionTime,
              "a=ice-pwd:session+level+icepwd\r\n"
              "a=ice-ufrag:session+level+iceufrag\r\n",
              &sdp_with_ufrag_pwd);
  // Add media level ufrag and pwd for audio
  InjectAfter(
      "a=mid:audio_content_name\r\n",
      "a=ice-pwd:media+level+icepwd\r\na=ice-ufrag:media+level+iceufrag\r\n",
      &sdp_with_ufrag_pwd);
  // Update the candidate ufrag and pwd to the expected ones.
  EXPECT_TRUE(UpdateCandidateUfragPwd(jdesc_.get(), 0, "media+level+iceufrag",
                                      "media+level+icepwd"));
  EXPECT_TRUE(UpdateCandidateUfragPwd(jdesc_.get(), 1, "session+level+iceufrag",
                                      "session+level+icepwd"));
  EXPECT_TRUE(
      CompareSessionDescription(jdesc_, SdpDeserialize(sdp_with_ufrag_pwd)));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRecvOnlyContent) {
  EXPECT_TRUE(TestDeserializeDirection(RtpTransceiverDirection::kRecvOnly));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithSendOnlyContent) {
  EXPECT_TRUE(TestDeserializeDirection(RtpTransceiverDirection::kSendOnly));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithInactiveContent) {
  EXPECT_TRUE(TestDeserializeDirection(RtpTransceiverDirection::kInactive));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRejectedAudio) {
  EXPECT_TRUE(TestDeserializeRejected(true, false));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRejectedVideo) {
  EXPECT_TRUE(TestDeserializeRejected(false, true));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithRejectedAudioVideo) {
  EXPECT_TRUE(TestDeserializeRejected(true, true));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithExtmapAllowMixed) {
  jdesc_->description()->set_extmap_allow_mixed(true);
  std::string sdp_with_extmap_allow_mixed = kSdpFullString;
  // Deserialize & Verify
  EXPECT_TRUE(CompareSessionDescription(
      jdesc_, SdpDeserialize(sdp_with_extmap_allow_mixed)));
}

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutExtmapAllowMixed) {
  jdesc_->description()->set_extmap_allow_mixed(false);
  std::string sdp_without_extmap_allow_mixed = kSdpFullString;
  Replace(kExtmapAllowMixed, "", &sdp_without_extmap_allow_mixed);
  // Deserialize & Verify
  EXPECT_TRUE(CompareSessionDescription(
      jdesc_, SdpDeserialize(sdp_without_extmap_allow_mixed)));
}

TEST_F(WebRtcSdpTest, DeserializeMediaContentDescriptionWithExtmapAllowMixed) {
  MediaContentDescription* video_desc =
      jdesc_->description()->GetContentDescriptionByName(kVideoContentName);
  ASSERT_THAT(video_desc, NotNull());
  MediaContentDescription* audio_desc =
      jdesc_->description()->GetContentDescriptionByName(kAudioContentName);
  ASSERT_THAT(audio_desc, NotNull());
  video_desc->set_extmap_allow_mixed_level(
      MediaContentDescription::AttributeLevel::kMedia);
  audio_desc->set_extmap_allow_mixed_level(
      MediaContentDescription::AttributeLevel::kMedia);

  std::string sdp_with_extmap_allow_mixed = kSdpFullString;
  InjectAfter("a=mid:audio_content_name\r\n", kExtmapAllowMixed,
              &sdp_with_extmap_allow_mixed);
  InjectAfter("a=mid:video_content_name\r\n", kExtmapAllowMixed,
              &sdp_with_extmap_allow_mixed);

  // Deserialize & Verify
  EXPECT_TRUE(CompareSessionDescription(
      jdesc_, SdpDeserialize(sdp_with_extmap_allow_mixed)));
}

TEST_F(WebRtcSdpTest, DeserializeCandidate) {
  std::string sdp = kSdpOneCandidate;
  std::unique_ptr<IceCandidate> jcandidate = NewCandidate(sdp);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(kDummyMid, jcandidate->sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate->sdp_mline_index());
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(jcandidate_->candidate()));
  EXPECT_EQ(0, jcandidate->candidate().network_cost());

  // Candidate line without generation extension.
  sdp = kSdpOneCandidate;
  Replace(" generation 2", "", &sdp);
  jcandidate = NewCandidate(sdp);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(kDummyMid, jcandidate->sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate->sdp_mline_index());
  Candidate expected = jcandidate_->candidate();
  expected.set_generation(0);
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(expected));

  // Candidate with network id and/or cost.
  sdp = kSdpOneCandidate;
  Replace(" generation 2", " generation 2 network-id 2", &sdp);
  jcandidate = NewCandidate(sdp);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(kDummyMid, jcandidate->sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate->sdp_mline_index());
  expected = jcandidate_->candidate();
  expected.set_network_id(2);
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(expected));
  EXPECT_EQ(0, jcandidate->candidate().network_cost());
  // Add network cost
  Replace(" network-id 2", " network-id 2 network-cost 9", &sdp);
  jcandidate = NewCandidate(sdp);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(expected));
  EXPECT_EQ(9, jcandidate->candidate().network_cost());

  sdp = kSdpTcpActiveCandidate;
  jcandidate = NewCandidate(sdp);
  ASSERT_THAT(jcandidate, NotNull());
  // Make a Candidate equivalent to kSdpTcpCandidate string.
  Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                      SocketAddress("192.168.1.5", 9), kCandidatePriority, "",
                      "", IceCandidateType::kHost, kCandidateGeneration,
                      kCandidateFoundation1);
  std::unique_ptr<IceCandidate> jcandidate_template(
      new IceCandidate(std::string("audio_content_name"), 0, candidate));
  EXPECT_TRUE(
      jcandidate->candidate().IsEquivalent(jcandidate_template->candidate()));
  ASSERT_THAT(NewCandidate(kSdpTcpPassiveCandidate), NotNull());
  ASSERT_THAT(NewCandidate(kSdpTcpSOCandidate), NotNull());
}

// This test verifies the deserialization of candidate-attribute
// as per RFC 5245. Candidate-attribute will be of the format
// candidate:<blah>. This format will be used when candidates
// are trickled.
TEST_F(WebRtcSdpTest, DeserializeRawCandidateAttribute) {
  std::string candidate_attribute = kRawCandidate;
  auto jcandidate = NewCandidate(candidate_attribute);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(kDummyMid, jcandidate->sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate->sdp_mline_index());
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(jcandidate_->candidate()));
  EXPECT_EQ(2u, jcandidate->candidate().generation());

  // Candidate line without generation extension.
  candidate_attribute = kRawCandidate;
  Replace(" generation 2", "", &candidate_attribute);
  jcandidate = NewCandidate(candidate_attribute);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(kDummyMid, jcandidate->sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate->sdp_mline_index());
  Candidate expected = jcandidate_->candidate();
  expected.set_generation(0);
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(expected));

  // Candidate line without candidate:
  candidate_attribute = kRawCandidate;
  Replace("candidate:", "", &candidate_attribute);
  ASSERT_THAT(NewCandidate(candidate_attribute), IsNull());

  // Candidate line with IPV6 address.
  ASSERT_TRUE(NewCandidate(kRawIPV6Candidate));

  // Candidate line with hostname address.
  ASSERT_THAT(NewCandidate(kRawHostnameCandidate), NotNull());
}

// This test verifies that the deserialization of an invalid candidate string
// fails.
TEST_F(WebRtcSdpTest, DeserializeInvalidCandidiate) {
  std::string candidate_attribute = kRawCandidate;
  ASSERT_THAT(NewCandidate(candidate_attribute), NotNull());

  candidate_attribute.replace(0, 1, "x");
  EXPECT_THAT(NewCandidate(candidate_attribute), IsNull());

  candidate_attribute = kSdpOneCandidate;
  candidate_attribute.replace(0, 1, "x");
  EXPECT_THAT(NewCandidate(candidate_attribute), IsNull());

  candidate_attribute = kRawCandidate;
  candidate_attribute.append("\r\n");
  candidate_attribute.append(kRawCandidate);
  EXPECT_THAT(NewCandidate(candidate_attribute), IsNull());
  EXPECT_THAT(NewCandidate(kSdpTcpInvalidCandidate), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannels) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelString);

  // Verify with UDP/DTLS/SCTP (already in kSdpSctpDataChannelString).
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      NewSessionDescriptionNoCandidates();
  EXPECT_TRUE(CompareSessionDescription(jdesc, SdpDeserialize(sdp_with_data)));

  // Verify with DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kUdpDtlsSctp), strlen(kUdpDtlsSctp),
                        kDtlsSctp);
  EXPECT_TRUE(CompareSessionDescription(jdesc, SdpDeserialize(sdp_with_data)));

  // Verify with TCP/DTLS/SCTP.
  sdp_with_data.replace(sdp_with_data.find(kDtlsSctp), strlen(kDtlsSctp),
                        kTcpDtlsSctp);
  EXPECT_TRUE(CompareSessionDescription(jdesc, SdpDeserialize(sdp_with_data)));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsWithSctpPort) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpPort);
  EXPECT_TRUE(
      MatchesCurrentDescriptionNoCandidates(SdpDeserialize(sdp_with_data)));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsWithSctpColonPort) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpColonPort);
  EXPECT_TRUE(
      MatchesCurrentDescriptionNoCandidates(SdpDeserialize(sdp_with_data)));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithNegativeSctpPort) {
  std::string sdp = kSdpString;
  sdp.append(kSdpSctpDataChannelStringWithSctpColonPort);
  SdpParseError error;
  absl::StrReplaceAll({{absl::StrCat(kDefaultSctpPort), absl::StrCat("-1")}},
                      &sdp);
  std::unique_ptr<SessionDescriptionInterface> output =
      SdpDeserialize(sdp, &error);
  ASSERT_THAT(output, IsNull());
  EXPECT_EQ(error.line, "a=sctp-port:-1");
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithTooLargeSctpPort) {
  std::string sdp = kSdpString;
  sdp.append(kSdpSctpDataChannelStringWithSctpColonPort);
  SdpParseError error;
  absl::StrReplaceAll({{absl::StrCat(kDefaultSctpPort), absl::StrCat("70000")}},
                      &sdp);
  std::unique_ptr<SessionDescriptionInterface> output =
      SdpDeserialize(sdp, &error);
  ASSERT_THAT(output, IsNull());
  EXPECT_EQ(error.line, "a=sctp-port:70000");
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithStringSctpPort) {
  std::string sdp = kSdpString;
  sdp.append(kSdpSctpDataChannelStringWithSctpColonPort);
  SdpParseError error;
  absl::StrReplaceAll(
      {{absl::StrCat(kDefaultSctpPort), absl::StrCat("webrtc")}}, &sdp);
  std::unique_ptr<SessionDescriptionInterface> output =
      SdpDeserialize(sdp, &error);
  ASSERT_THAT(output, IsNull());
  EXPECT_EQ(error.line, "a=sctp-port:webrtc");
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsWithSctpInit) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);

  std::unique_ptr<SessionDescriptionInterface> jdesc =
      NewSessionDescriptionNoCandidates();
  ASSERT_THAT(jdesc, NotNull());
  SctpDataContentDescription* dcdesc =
      jdesc->description()
          ->GetContentDescriptionByName(kDataContentName)
          ->as_sctp();
  // base64("CookieMonster")
  std::vector<uint8_t> cookie_monster = {0x43, 0x6f, 0x6f, 0x6b, 0x69,
                                         0x65, 0x4d, 0x6f, 0x6e, 0x73,
                                         0x74, 0x65, 0x72};
  dcdesc->set_sctp_init(cookie_monster);

  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpInit);

  SdpParseError error;
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp_with_data, &error);
  ASSERT_THAT(jdesc, NotNull());
  EXPECT_TRUE(CompareSessionDescription(jdesc, jdesc_output));
}

TEST_F(WebRtcSdpTest,
       DeserializeSdpWithSctpDataChannelsWithSctpInitWithoutParams) {
  std::string invalid_line = "a=sctp-init";
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpColonPort);
  sdp_with_data.append(invalid_line + "\r\n");

  SdpParseError error;
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(sdp_with_data, &error);
  EXPECT_THAT(jdesc, IsNull());
  EXPECT_EQ(invalid_line, error.line);
  EXPECT_EQ("Failed to get the value of attribute: sctp-init",
            error.description);
}

TEST_F(WebRtcSdpTest,
       DeserializeSdpWithSctpDataChannelsWithSctpInitInvalidBase64) {
  std::string invalid_line = "a=sctp-init:*";
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpColonPort);
  sdp_with_data.append(invalid_line + "\r\n");

  SdpParseError error;
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(sdp_with_data, &error);
  EXPECT_THAT(jdesc, IsNull());
  EXPECT_EQ(invalid_line, error.line);
  EXPECT_EQ("Base64 decoding of sctp-init failed.", error.description);
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsButWrongMediaType) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);

  std::string sdp = kSdpSessionString;
  sdp += kSdpSctpDataChannelString;

  const char needle[] = "m=application ";
  sdp.replace(sdp.find(needle), strlen(needle), "m=application:bogus ");

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());

  EXPECT_EQ(1u, jdesc_output->description()->contents().size());
  EXPECT_TRUE(jdesc_output->description()->contents()[0].rejected);
}

// Helper function to set the max-message-size parameter in the
// SCTP data codec.
std::unique_ptr<SessionDescriptionInterface>
CreateSessionDescriptionWithSctpMaxMessageSize(const SessionDescription& desc,
                                               int new_value) {
  std::unique_ptr<SessionDescription> mutant = desc.Clone();
  SctpDataContentDescription* dcdesc =
      mutant->GetContentDescriptionByName(kDataContentName)->as_sctp();
  dcdesc->set_max_message_size(new_value);
  return CreateSessionDescription(kDummyType, kSessionId, kSessionVersion,
                                  std::move(mutant));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsWithMaxMessageSize) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpColonPort);
  sdp_with_data.append("a=max-message-size:12345\r\n");
  EXPECT_TRUE(CompareSessionDescription(
      CreateSessionDescriptionWithSctpMaxMessageSize(desc_, 12345),
      SdpDeserialize(sdp_with_data)));
}

TEST_F(WebRtcSdpTest,
       DeserializeSdpWithSctpDataChannelsWithInvalidMaxMessageSize) {
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpColonPort);
  sdp_with_data.append("a=max-message-size:-1\r\n");
  SdpParseError error;
  std::unique_ptr<SessionDescriptionInterface> output =
      SdpDeserialize(sdp_with_data, &error);
  ASSERT_THAT(output, IsNull());
  EXPECT_EQ(error.line, "a=max-message-size:-1");
}

TEST_F(WebRtcSdpTest, SerializeSdpWithSctpDataChannelWithMaxMessageSize) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      CreateSessionDescriptionWithSctpMaxMessageSize(desc_, 12345);
  std::string message = SdpSerialize(jdesc);
  EXPECT_NE(std::string::npos,
            message.find("\r\na=max-message-size:12345\r\n"));
  EXPECT_TRUE(CompareSessionDescription(jdesc, SdpDeserialize(message)));
}

TEST_F(WebRtcSdpTest,
       SerializeSdpWithSctpDataChannelWithDefaultMaxMessageSize) {
  // https://tools.ietf.org/html/draft-ietf-mmusic-sctp-sdp-26#section-6
  // The default max message size is 64K.
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      CreateSessionDescriptionWithSctpMaxMessageSize(desc_, 65536);
  std::string message = SdpSerialize(jdesc);
  EXPECT_EQ(std::string::npos, message.find("\r\na=max-message-size:"));
  EXPECT_TRUE(CompareSessionDescription(jdesc, SdpDeserialize(message)));
}

// Test to check the behaviour if sctp-port is specified
// on the m= line and in a=sctp-port.
TEST_F(WebRtcSdpTest, DeserializeSdpWithMultiSctpPort) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  std::string sdp_with_data = kSdpString;
  // Append m= attributes
  sdp_with_data.append(kSdpSctpDataChannelString);
  // Append a=sctp-port attribute
  sdp_with_data.append("a=sctp-port 5000\r\n");
  EXPECT_THAT(SdpDeserialize(sdp_with_data), IsNull());
}

// Test behavior if a=rtpmap occurs in an SCTP section.
TEST_F(WebRtcSdpTest, DeserializeSdpWithRtpmapAttribute) {
  std::string sdp_with_data = kSdpString;
  // Append m= attributes
  sdp_with_data.append(kSdpSctpDataChannelString);
  // Append a=rtpmap attribute
  sdp_with_data.append("a=rtpmap:111 opus/48000/2\r\n");
  // Correct behavior is to ignore the extra attribute.
  EXPECT_THAT(SdpDeserialize(sdp_with_data), NotNull());
}

// For crbug/344475.
TEST_F(WebRtcSdpTest, DeserializeSdpWithCorruptedSctpDataChannels) {
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelString);
  // Remove the "\n" at the end.
  sdp_with_data = sdp_with_data.substr(0, sdp_with_data.size() - 1);
  EXPECT_THAT(SdpDeserialize(sdp_with_data), IsNull());
  // No crash is a pass.
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelAndUnusualPort) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);

  // First setup the expected session description.

  // Then get the deserialized session description.
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelString);
  absl::StrReplaceAll(
      {{absl::StrCat(kDefaultSctpPort), absl::StrCat(kUnusualSctpPort)}},
      &sdp_with_data);
  EXPECT_TRUE(CompareSessionDescription(
      MutateSctpPort(jdesc_.get(), desc_, kUnusualSctpPort),
      SdpDeserialize(sdp_with_data)));
}

TEST_F(WebRtcSdpTest,
       DeserializeSdpWithSctpDataChannelAndUnusualPortInAttribute) {
  bool use_sctpmap = false;
  AddSctpDataChannel(use_sctpmap);

  // We need to test the deserialized description from
  // kSdpSctpDataChannelStringWithSctpPort for
  // draft-ietf-mmusic-sctp-sdp-07
  // a=sctp-port
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelStringWithSctpPort);
  absl::StrReplaceAll(
      {{absl::StrCat(kDefaultSctpPort), absl::StrCat(kUnusualSctpPort)}},
      &sdp_with_data);
  EXPECT_TRUE(CompareSessionDescription(
      MutateSctpPort(jdesc_.get(), desc_, kUnusualSctpPort),
      SdpDeserialize(sdp_with_data)));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithSctpDataChannelsAndBandwidth) {
  bool use_sctpmap = true;
  AddSctpDataChannel(use_sctpmap);
  SctpDataContentDescription* dcd = GetFirstSctpDataContentDescription(&desc_);
  dcd->set_bandwidth(100 * 1000);

  std::string sdp_with_bandwidth = kSdpString;
  sdp_with_bandwidth.append(kSdpSctpDataChannelString);
  InjectAfter("a=mid:data_content_name\r\n", "b=AS:100\r\n",
              &sdp_with_bandwidth);
  // SCTP has congestion control, so we shouldn't limit the bandwidth
  // as we do for RTP.
  EXPECT_TRUE(MatchesCurrentDescriptionNoCandidates(
      SdpDeserialize(sdp_with_bandwidth)));
}

class WebRtcSdpExtmapTest : public WebRtcSdpTest,
                            public ::testing::WithParamInterface<bool> {};

TEST_P(WebRtcSdpExtmapTest,
       DeserializeSessionDescriptionWithSessionLevelExtmap) {
  bool encrypted = GetParam();
  TestDeserializeExtmap(true, false, encrypted);
}

TEST_P(WebRtcSdpExtmapTest, DeserializeSessionDescriptionWithMediaLevelExtmap) {
  bool encrypted = GetParam();
  TestDeserializeExtmap(false, true, encrypted);
}

TEST_P(WebRtcSdpExtmapTest, DeserializeSessionDescriptionWithInvalidExtmap) {
  bool encrypted = GetParam();
  TestDeserializeExtmap(true, true, encrypted);
}

INSTANTIATE_TEST_SUITE_P(Encrypted,
                         WebRtcSdpExtmapTest,
                         ::testing::Values(false, true));

TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutEndLineBreak) {
  std::string sdp = kSdpFullString;
  sdp = sdp.substr(0, sdp.size() - 2);  // Remove \r\n at the end.
  // Deserialize
  SdpParseError error;
  EXPECT_THAT(SdpDeserialize(sdp, &error), IsNull());
  const std::string lastline = "a=ssrc:3 cname:stream_1_cname";
  EXPECT_EQ(lastline, error.line);
  EXPECT_EQ("Invalid SDP line.", error.description);
}

TEST_F(WebRtcSdpTest, DeserializeCandidateWithDifferentTransport) {
  std::string new_sdp = kSdpOneCandidate;
  Replace("udp", "unsupported_transport", &new_sdp);
  EXPECT_THAT(NewCandidate(new_sdp), IsNull());
  new_sdp = kSdpOneCandidate;
  Replace("udp", "uDP", &new_sdp);
  auto jcandidate = NewCandidate(new_sdp);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(kDummyMid, jcandidate->sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate->sdp_mline_index());
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(jcandidate_->candidate()));
}

TEST_F(WebRtcSdpTest, DeserializeCandidateWithUfragPwd) {
  auto jcandidate = NewCandidate(kSdpOneCandidateWithUfragPwd);
  ASSERT_THAT(jcandidate, NotNull());
  EXPECT_EQ(kDummyMid, jcandidate->sdp_mid());
  EXPECT_EQ(kDummyIndex, jcandidate->sdp_mline_index());
  Candidate ref_candidate = jcandidate_->candidate();
  ref_candidate.set_username("user_rtp");
  ref_candidate.set_password("password_rtp");
  EXPECT_TRUE(jcandidate->candidate().IsEquivalent(ref_candidate));
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithConferenceFlag) {
  // Deserialize
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(kSdpConferenceString);
  ASSERT_THAT(jdesc, NotNull());

  // Verify
  AudioContentDescription* audio =
      GetFirstAudioContentDescription(jdesc->description());
  EXPECT_TRUE(audio->conference_mode());

  VideoContentDescription* video =
      GetFirstVideoContentDescription(jdesc->description());
  EXPECT_TRUE(video->conference_mode());
}

TEST_F(WebRtcSdpTest, SerializeSdpWithConferenceFlag) {
  // We tested deserialization already above, so just test that if we serialize
  // and deserialize the flag doesn't disappear.
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(kSdpConferenceString);
  ASSERT_THAT(jdesc, NotNull());
  std::string reserialized = SdpSerialize(jdesc);
  jdesc = SdpDeserialize(reserialized);
  ASSERT_THAT(jdesc, NotNull());

  // Verify.
  AudioContentDescription* audio =
      GetFirstAudioContentDescription(jdesc->description());
  EXPECT_TRUE(audio->conference_mode());

  VideoContentDescription* video =
      GetFirstVideoContentDescription(jdesc->description());
  EXPECT_TRUE(video->conference_mode());
}

TEST_F(WebRtcSdpTest, SerializeAndDeserializeRemoteNetEstimate) {
  {
    // By default remote estimates are disabled.
    std::unique_ptr<SessionDescriptionInterface> dst =
        SdpDeserialize(SdpSerialize(jdesc_));
    ASSERT_THAT(dst, NotNull());
    EXPECT_FALSE(
        GetFirstVideoContentDescription(dst->description())->remote_estimate());
  }
  {
    // When remote estimate is enabled, the setting is propagated via SDP.
    GetFirstVideoContentDescription(jdesc_->description())
        ->set_remote_estimate(true);
    std::unique_ptr<SessionDescriptionInterface> dst =
        SdpDeserialize(SdpSerialize(jdesc_));
    ASSERT_THAT(dst, NotNull());
    EXPECT_TRUE(
        GetFirstVideoContentDescription(dst->description())->remote_estimate());
  }
}

TEST_F(WebRtcSdpTest, DeserializeBrokenSdp) {
  const char kSdpDestroyer[] = "!@#$%^&";
  const char kSdpEmptyType[] = " =candidate";
  const char kSdpEqualAsPlus[] = "a+candidate";
  const char kSdpSpaceAfterEqual[] = "a= candidate";
  const char kSdpUpperType[] = "A=candidate";
  const char kSdpEmptyLine[] = "";
  const char kSdpMissingValue[] = "a=";

  const char kSdpBrokenFingerprint[] =
      "a=fingerprint:sha-1 "
      "4AAD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB";
  const char kSdpExtraField[] =
      "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB XXX";
  const char kSdpMissingSpace[] =
      "a=fingerprint:sha-1"
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB";
  // MD5 is not allowed in fingerprints.
  const char kSdpMd5[] =
      "a=fingerprint:md5 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B";

  // Broken session description
  ExpectParseFailure("v=", kSdpDestroyer);
  ExpectParseFailure("o=", kSdpDestroyer);
  ExpectParseFailure("s=-", kSdpDestroyer);
  // Broken time description
  ExpectParseFailure("t=", kSdpDestroyer);

  // Broken media description
  ExpectParseFailure("m=audio", "c=IN IP4 74.125.224.39");
  ExpectParseFailure("m=video", kSdpDestroyer);
  ExpectParseFailure("m=", "c=IN IP4 74.125.224.39");

  // Invalid lines
  ExpectParseFailure("a=candidate", kSdpEmptyType);
  ExpectParseFailure("a=candidate", kSdpEqualAsPlus);
  ExpectParseFailure("a=candidate", kSdpSpaceAfterEqual);
  ExpectParseFailure("a=candidate", kSdpUpperType);

  // Bogus fingerprint replacing a=sendrev. We selected this attribute
  // because it's orthogonal to what we are replacing and hence
  // safe.
  ExpectParseFailure("a=sendrecv", kSdpBrokenFingerprint);
  ExpectParseFailure("a=sendrecv", kSdpExtraField);
  ExpectParseFailure("a=sendrecv", kSdpMissingSpace);
  ExpectParseFailure("a=sendrecv", kSdpMd5);

  // Empty Line
  ExpectParseFailure("a=rtcp:2347 IN IP4 74.125.127.126", kSdpEmptyLine);
  ExpectParseFailure("a=rtcp:2347 IN IP4 74.125.127.126", kSdpMissingValue);
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithInvalidAttributeValue) {
  // ssrc
  ExpectParseFailure("a=ssrc:1", "a=ssrc:badvalue");
  ExpectParseFailure("a=ssrc-group:FEC 2 3", "a=ssrc-group:FEC badvalue 3");
  ExpectParseFailure(
      "a=ssrc-group:FEC 2 3",
      "a=ssrc-group:FEC 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 "
      "22 23 24 25 26 27 28 29 30 31 32 33 34 35");
  // rtpmap
  ExpectParseFailure("a=rtpmap:111 ", "a=rtpmap:badvalue ");
  ExpectParseFailure("opus/48000/2", "opus/badvalue/2");
  ExpectParseFailure("opus/48000/2", "opus/48000/badvalue");
  // candidate
  ExpectParseFailure("1 udp 2130706432", "badvalue udp 2130706432");
  ExpectParseFailure("1 udp 2130706432", "1 udp badvalue");
  ExpectParseFailure("192.168.1.5 1234", "192.168.1.5 badvalue");
  ExpectParseFailure("rport 2346", "rport badvalue");
  ExpectParseFailure("rport 2346 generation 2",
                     "rport 2346 generation badvalue");
  // m line
  ExpectParseFailure("m=audio 2345 RTP/SAVPF 111 103 104",
                     "m=audio 2345 RTP/SAVPF 111 badvalue 104");

  // bandwidth
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "b=AS:badvalue\r\n", "b=AS:badvalue");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "b=AS\r\n",
                                 "b=AS");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n", "b=AS:\r\n",
                                 "b=AS:");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "b=AS:12:34\r\n", "b=AS:12:34");

  // rtcp-fb
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=rtcp-fb:badvalue nack\r\n",
                                 "a=rtcp-fb:badvalue nack");
  // extmap
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:badvalue http://example.com\r\n",
                                 "a=extmap:badvalue http://example.com");
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithInvalidExtmapUri) {
  // Test raw control characters
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/\x01\r\n",
                                 "a=extmap:1 http://example.com/\x01");

  // Test percent-encoded control characters
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/%0a\r\n",
                                 "a=extmap:1 http://example.com/%0a");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/%0d\r\n",
                                 "a=extmap:1 http://example.com/%0d");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/%1f\r\n",
                                 "a=extmap:1 http://example.com/%1f");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/%7f\r\n",
                                 "a=extmap:1 http://example.com/%7f");

  // Test invalid percent encoding
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/%\r\n",
                                 "a=extmap:1 http://example.com/%");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/%7\r\n",
                                 "a=extmap:1 http://example.com/%7");
  ExpectParseFailureWithNewLines("a=mid:video_content_name\r\n",
                                 "a=extmap:1 http://example.com/%7g\r\n",
                                 "a=extmap:1 http://example.com/%7g");

  // Test valid percent-encoded characters (should succeed)
  std::string sdp_with_valid_percent =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 9 RTP/SAVPF 100\r\n"
      "a=mid:video_content_name\r\n"
      "a=extmap:1 http://example.com/foo%20bar\r\n";
  EXPECT_THAT(SdpDeserialize(sdp_with_valid_percent), NotNull());
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithReorderedPltypes) {
  const char kSdpWithReorderedPlTypesString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 104 103\r\n"  // Pl type 104 preferred.
      "a=rtpmap:111 opus/48000/2\r\n"    // Pltype 111 listed before 103 and 104
                                         // in the map.
      "a=rtpmap:103 ISAC/16000\r\n"  // Pltype 103 listed before 104 in the map.
      "a=rtpmap:104 ISAC/32000\r\n";

  // Deserialize
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(kSdpWithReorderedPlTypesString);
  ASSERT_THAT(jdesc_output, NotNull());

  const AudioContentDescription* acd =
      GetFirstAudioContentDescription(jdesc_output->description());
  ASSERT_THAT(acd, NotNull());
  ASSERT_FALSE(acd->codecs().empty());
  EXPECT_EQ("ISAC", acd->codecs()[0].name);
  EXPECT_EQ(32000, acd->codecs()[0].clockrate);
  EXPECT_EQ(104, acd->codecs()[0].id);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeCodecParams) {
  CodecParams params;
  params.max_ptime = 40;
  params.ptime = 30;
  params.min_ptime = 10;
  params.sprop_stereo = 1;
  params.stereo = 1;
  params.useinband = 1;
  params.maxaveragebitrate = 128000;
  std::unique_ptr<SessionDescriptionInterface> jdesc_output;
  TestDeserializeCodecParams(params, jdesc_output);
  TestSerialize(jdesc_output);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeRtcpFb) {
  const bool kUseWildcard = false;
  std::unique_ptr<SessionDescriptionInterface> jdesc_output;
  TestDeserializeRtcpFb(jdesc_output, kUseWildcard, /* use_ccfb= */ false);
  TestSerialize(jdesc_output);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeRtcpFbWildcard) {
  const bool kUseWildcard = true;
  std::unique_ptr<SessionDescriptionInterface> jdesc_output;
  TestDeserializeRtcpFb(jdesc_output, kUseWildcard, /* use_ccfb= */ false);
  TestSerialize(jdesc_output);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeRtcpWithCcfb) {
  std::unique_ptr<SessionDescriptionInterface> jdesc_output;
  TestDeserializeRtcpFb(jdesc_output, /* use_wildcard= */ false,
                        /* use_ccfb= */ true);
  TestSerialize(jdesc_output);
  TestDeserializeRtcpFb(jdesc_output, /* use_wildcard= */ true,
                        /* use_ccfb= */ true);
  TestSerialize(jdesc_output);
}

TEST_F(WebRtcSdpTest, DeserializeSerializeRtcpXrRcvrRtt) {
  const char kSdpWithRcvrRtt[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=rtcp-xr:rcvr-rtt=all\r\n";

  // Deserialize: rcvr-rtt sets the single receive-non-sender-RTT flag.
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(kSdpWithRcvrRtt);
  ASSERT_THAT(jdesc, NotNull());
  const AudioContentDescription* acd =
      GetFirstAudioContentDescription(jdesc->description());
  ASSERT_THAT(acd, NotNull());
  EXPECT_TRUE(acd->receive_non_sender_rtt());

  // Serialize: the flag emits the standard rtcp-xr form only. The legacy
  // rtcp-fb rrtr is never generated from it.
  std::string reserialized = SdpSerialize(*jdesc);
  EXPECT_THAT(reserialized, HasSubstr("a=rtcp-xr:rcvr-rtt=all"));
  EXPECT_THAT(reserialized, Not(HasSubstr("rrtr")));
}

// The legacy non-standard a=rtcp-fb:<pt> rrtr stays a codec feedback param,
// separate from the rcvr-rtt flag, and round-trips unchanged.
TEST_F(WebRtcSdpTest, DeserializeSerializeLegacyRtcpFbRrtr) {
  const char kSdpWithFbRrtr[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=rtcp-fb:111 rrtr\r\n";
  std::unique_ptr<SessionDescriptionInterface> jdesc =
      SdpDeserialize(kSdpWithFbRrtr);
  ASSERT_THAT(jdesc, NotNull());
  const AudioContentDescription* acd =
      GetFirstAudioContentDescription(jdesc->description());
  ASSERT_THAT(acd, NotNull());
  EXPECT_FALSE(acd->receive_non_sender_rtt());
  ASSERT_THAT(acd->codecs(), SizeIs(1));
  EXPECT_TRUE(acd->codecs()[0].HasFeedbackParam(
      FeedbackParam(kRtcpFbParamRrtr, kParamValueEmpty)));

  std::string reserialized = SdpSerialize(*jdesc);
  EXPECT_THAT(reserialized, HasSubstr("a=rtcp-fb:111 rrtr"));
  EXPECT_THAT(reserialized, Not(HasSubstr("a=rtcp-xr")));
}

TEST_F(WebRtcSdpTest, DeserializeVideoFmtp) {
  const char kSdpWithFmtpString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "a=rtpmap:120 VP8/90000\r\n"
      "a=fmtp:120 x-google-min-bitrate=10;x-google-max-quantization=40\r\n";

  // Deserialize
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(kSdpWithFmtpString);
  ASSERT_THAT(jdesc_output, NotNull());

  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output->description());
  ASSERT_THAT(vcd, NotNull());
  ASSERT_FALSE(vcd->codecs().empty());
  Codec vp8 = vcd->codecs()[0];
  EXPECT_EQ("VP8", vp8.name);
  EXPECT_EQ(120, vp8.id);
  CodecParameterMap::iterator found = vp8.params.find("x-google-min-bitrate");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "10");
  found = vp8.params.find("x-google-max-quantization");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "40");
}

TEST_F(WebRtcSdpTest, DeserializeVideoFmtpWithSprops) {
  const char kSdpWithFmtpString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 49170 RTP/AVP 98\r\n"
      "a=rtpmap:98 H264/90000\r\n"
      "a=fmtp:98 profile-level-id=42A01E; "
      "sprop-parameter-sets=Z0IACpZTBYmI,aMljiA==\r\n";

  // Deserialize.
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(kSdpWithFmtpString);
  ASSERT_THAT(jdesc_output, NotNull());

  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output->description());
  ASSERT_THAT(vcd, NotNull());
  ASSERT_FALSE(vcd->codecs().empty());
  Codec h264 = vcd->codecs()[0];
  EXPECT_EQ("H264", h264.name);
  EXPECT_EQ(98, h264.id);
  CodecParameterMap::const_iterator found =
      h264.params.find("profile-level-id");
  ASSERT_TRUE(found != h264.params.end());
  EXPECT_EQ(found->second, "42A01E");
  found = h264.params.find("sprop-parameter-sets");
  ASSERT_TRUE(found != h264.params.end());
  EXPECT_EQ(found->second, "Z0IACpZTBYmI,aMljiA==");
}

TEST_F(WebRtcSdpTest, DeserializeVideoFmtpWithSpace) {
  const char kSdpWithFmtpString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "a=rtpmap:120 VP8/90000\r\n"
      "a=fmtp:120   x-google-min-bitrate=10;  x-google-max-quantization=40\r\n";

  // Deserialize
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(kSdpWithFmtpString);
  ASSERT_THAT(jdesc_output, NotNull());

  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output->description());
  ASSERT_THAT(vcd, NotNull());
  ASSERT_FALSE(vcd->codecs().empty());
  Codec vp8 = vcd->codecs()[0];
  EXPECT_EQ("VP8", vp8.name);
  EXPECT_EQ(120, vp8.id);
  CodecParameterMap::iterator found = vp8.params.find("x-google-min-bitrate");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "10");
  found = vp8.params.find("x-google-max-quantization");
  ASSERT_TRUE(found != vp8.params.end());
  EXPECT_EQ(found->second, "40");
}

TEST_F(WebRtcSdpTest, DeserializePacketizationAttributeWithIllegalValue) {
  const char kSdpWithPacketizationString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=packetization:111 unknownpacketizationattributeforaudio\r\n"
      "m=video 3457 RTP/SAVPF 120 121 122\r\n"
      "a=rtpmap:120 VP8/90000\r\n"
      "a=packetization:120 raw\r\n"
      "a=rtpmap:121 VP9/90000\r\n"
      "a=rtpmap:122 H264/90000\r\n"
      "a=packetization:122 unknownpacketizationattributevalue\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(kSdpWithPacketizationString);
  ASSERT_THAT(jdesc_output, NotNull());

  AudioContentDescription* acd =
      GetFirstAudioContentDescription(jdesc_output->description());
  ASSERT_THAT(acd, NotNull());
  ASSERT_THAT(acd->codecs(), testing::SizeIs(1));
  Codec opus = acd->codecs()[0];
  EXPECT_EQ(opus.name, "opus");
  EXPECT_EQ(opus.id, 111);

  const VideoContentDescription* vcd =
      GetFirstVideoContentDescription(jdesc_output->description());
  ASSERT_THAT(vcd, NotNull());
  ASSERT_THAT(vcd->codecs(), testing::SizeIs(3));
  Codec vp8 = vcd->codecs()[0];
  EXPECT_EQ(vp8.name, "VP8");
  EXPECT_EQ(vp8.id, 120);
  EXPECT_EQ(vp8.packetization, "raw");
  Codec vp9 = vcd->codecs()[1];
  EXPECT_EQ(vp9.name, "VP9");
  EXPECT_EQ(vp9.id, 121);
  EXPECT_EQ(vp9.packetization, std::nullopt);
  Codec h264 = vcd->codecs()[2];
  EXPECT_EQ(h264.name, "H264");
  EXPECT_EQ(h264.id, 122);
  EXPECT_EQ(h264.packetization, std::nullopt);
}

TEST_F(WebRtcSdpTest, SerializeAudioFmtpWithUnknownParameter) {
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);

  Codecs codecs = acd->codecs();
  codecs[0].params["unknown-future-parameter"] = "SomeFutureValue";
  acd->set_codecs(codecs);

  std::string message = SerializeCurrentDescription();
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:111 opus/48000/2\r\n",
              "a=fmtp:111 unknown-future-parameter=SomeFutureValue\r\n",
              &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeAudioFmtpWithKnownFmtpParameter) {
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);

  Codecs codecs = acd->codecs();
  codecs[0].params["stereo"] = "1";
  acd->set_codecs(codecs);

  std::string message = SerializeCurrentDescription();
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:111 opus/48000/2\r\n", "a=fmtp:111 stereo=1\r\n",
              &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeAudioFmtpWithPTimeAndMaxPTime) {
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);

  Codecs codecs = acd->codecs();
  codecs[0].params["ptime"] = "20";
  codecs[0].params["maxptime"] = "120";
  acd->set_codecs(codecs);

  std::string message = SerializeCurrentDescription();
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:104 ISAC/32000\r\n",
              "a=maxptime:120\r\n"  // No comma here. String merging!
              "a=ptime:20\r\n",
              &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeAudioFmtpWithTelephoneEvent) {
  AudioContentDescription* acd = GetFirstAudioContentDescription(&desc_);

  Codecs codecs = acd->codecs();
  Codec dtmf = CreateAudioCodec(105, "telephone-event", 8000, 1);
  dtmf.params[""] = "0-15";
  codecs.push_back(dtmf);
  acd->set_codecs(codecs);

  std::string message = SerializeCurrentDescription();
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("m=audio 2345 RTP/SAVPF 111 103 104", " 105", &sdp_with_fmtp);
  InjectAfter(
      "a=rtpmap:104 ISAC/32000\r\n",
      "a=rtpmap:105 telephone-event/8000\r\n"  // No comma here. String merging!
      "a=fmtp:105 0-15\r\n",
      &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeVideoFmtp) {
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);

  Codecs codecs = vcd->codecs();
  codecs[0].params["x-google-min-bitrate"] = "10";
  vcd->set_codecs(codecs);

  std::string message = SerializeCurrentDescription();
  std::string sdp_with_fmtp = kSdpFullString;
  InjectAfter("a=rtpmap:120 VP8/90000\r\n",
              "a=fmtp:120 x-google-min-bitrate=10\r\n", &sdp_with_fmtp);
  EXPECT_EQ(sdp_with_fmtp, message);
}

TEST_F(WebRtcSdpTest, SerializeVideoPacketizationAttribute) {
  VideoContentDescription* vcd = GetFirstVideoContentDescription(&desc_);

  Codecs codecs = vcd->codecs();
  codecs[0].packetization = "raw";
  vcd->set_codecs(codecs);

  std::string message = SerializeCurrentDescription();
  std::string sdp_with_packetization = kSdpFullString;
  InjectAfter("a=rtpmap:120 VP8/90000\r\n", "a=packetization:120 raw\r\n",
              &sdp_with_packetization);
  EXPECT_EQ(sdp_with_packetization, message);
}

TEST_F(WebRtcSdpTest, DeserializeAndSerializeSdpWithIceLite) {
  // Deserialize the baseline description, making sure it's ICE full.
  std::string sdp_with_icelite = kSdpFullString;
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_icelite =
      SdpDeserialize(sdp_with_icelite);
  ASSERT_THAT(jdesc_with_icelite, NotNull());
  SessionDescription* desc = jdesc_with_icelite->description();
  const TransportInfo* tinfo1 =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(ICEMODE_FULL, tinfo1->description.ice_mode);
  const TransportInfo* tinfo2 =
      desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(ICEMODE_FULL, tinfo2->description.ice_mode);

  // Add "a=ice-lite" and deserialize, making sure it's ICE lite.
  InjectAfter(kSessionTime, "a=ice-lite\r\n", &sdp_with_icelite);
  jdesc_with_icelite = SdpDeserialize(sdp_with_icelite);
  ASSERT_THAT(jdesc_with_icelite, NotNull());
  desc = jdesc_with_icelite->description();
  const TransportInfo* atinfo =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(ICEMODE_LITE, atinfo->description.ice_mode);
  const TransportInfo* vtinfo =
      desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(ICEMODE_LITE, vtinfo->description.ice_mode);

  // Now that we know deserialization works, we can use TestSerialize to test
  // serialization.
  TestSerialize(jdesc_with_icelite);
}

// Verifies that the candidates in the input SDP are parsed and serialized
// correctly in the output SDP.
TEST_F(WebRtcSdpTest, RoundTripSdpWithSctpDataChannelsWithCandidates) {
  std::string sdp_with_data = kSdpString;
  sdp_with_data.append(kSdpSctpDataChannelWithCandidatesString);
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp_with_data);
  ASSERT_THAT(jdesc_output, NotNull());
  EXPECT_EQ(sdp_with_data, SdpSerialize(jdesc_output));
}

TEST_F(WebRtcSdpTest, SerializeDtlsSetupAttribute) {
  TransportInfo audio_transport_info =
      *(desc_.GetTransportInfoByName(kAudioContentName));
  EXPECT_EQ(CONNECTIONROLE_NONE,
            audio_transport_info.description.connection_role);
  audio_transport_info.description.connection_role = CONNECTIONROLE_ACTIVE;

  TransportInfo video_transport_info =
      *(desc_.GetTransportInfoByName(kVideoContentName));
  EXPECT_EQ(CONNECTIONROLE_NONE,
            video_transport_info.description.connection_role);
  video_transport_info.description.connection_role = CONNECTIONROLE_ACTIVE;

  desc_.RemoveTransportInfoByName(kAudioContentName);
  desc_.RemoveTransportInfoByName(kVideoContentName);

  desc_.AddTransportInfo(audio_transport_info);
  desc_.AddTransportInfo(video_transport_info);

  std::string message = SerializeCurrentDescription();
  std::string sdp_with_dtlssetup = kSdpFullString;

  // Now adding `setup` attribute.
  InjectAfter(kFingerprint, "a=setup:active\r\n", &sdp_with_dtlssetup);
  EXPECT_EQ(sdp_with_dtlssetup, message);
}

TEST_F(WebRtcSdpTest, DeserializeDtlsSetupAttributeActpass) {
  std::string sdp_with_dtlssetup = kSdpFullString;
  InjectAfter(kSessionTime, "a=setup:actpass\r\n", &sdp_with_dtlssetup);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_dtlssetup =
      SdpDeserialize(sdp_with_dtlssetup);
  ASSERT_THAT(jdesc_with_dtlssetup, NotNull());
  SessionDescription* desc = jdesc_with_dtlssetup->description();
  const TransportInfo* atinfo =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(CONNECTIONROLE_ACTPASS, atinfo->description.connection_role);
  const TransportInfo* vtinfo =
      desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(CONNECTIONROLE_ACTPASS, vtinfo->description.connection_role);
}

TEST_F(WebRtcSdpTest, DeserializeDtlsSetupAttributeActive) {
  std::string sdp_with_dtlssetup = kSdpFullString;
  InjectAfter(kSessionTime, "a=setup:active\r\n", &sdp_with_dtlssetup);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_dtlssetup =
      SdpDeserialize(sdp_with_dtlssetup);
  ASSERT_THAT(jdesc_with_dtlssetup, NotNull());
  SessionDescription* desc = jdesc_with_dtlssetup->description();
  const TransportInfo* atinfo =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(CONNECTIONROLE_ACTIVE, atinfo->description.connection_role);
  const TransportInfo* vtinfo =
      desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(CONNECTIONROLE_ACTIVE, vtinfo->description.connection_role);
}
TEST_F(WebRtcSdpTest, DeserializeDtlsSetupAttributePassive) {
  std::string sdp_with_dtlssetup = kSdpFullString;
  InjectAfter(kSessionTime, "a=setup:passive\r\n", &sdp_with_dtlssetup);
  std::unique_ptr<SessionDescriptionInterface> jdesc_with_dtlssetup =
      SdpDeserialize(sdp_with_dtlssetup);
  ASSERT_THAT(jdesc_with_dtlssetup, NotNull());
  SessionDescription* desc = jdesc_with_dtlssetup->description();
  const TransportInfo* atinfo =
      desc->GetTransportInfoByName("audio_content_name");
  EXPECT_EQ(CONNECTIONROLE_PASSIVE, atinfo->description.connection_role);
  const TransportInfo* vtinfo =
      desc->GetTransportInfoByName("video_content_name");
  EXPECT_EQ(CONNECTIONROLE_PASSIVE, vtinfo->description.connection_role);
}

// Verifies that the order of the serialized m-lines follows the order of the
// ContentInfo in SessionDescription, and vise versa for deserialization.
TEST_F(WebRtcSdpTest, MediaContentOrderMaintainedRoundTrip) {
  const std::string media_content_sdps[3] = {kSdpAudioString, kSdpVideoString,
                                             kSdpSctpDataChannelString};
  const MediaType media_types[3] = {MediaType::AUDIO, MediaType::VIDEO,
                                    MediaType::DATA};

  // Verifies all 6 permutations.
  for (size_t i = 0; i < 6; ++i) {
    size_t media_content_in_sdp[3];
    // The index of the first media content.
    media_content_in_sdp[0] = i / 2;
    // The index of the second media content.
    media_content_in_sdp[1] = (media_content_in_sdp[0] + i % 2 + 1) % 3;
    // The index of the third media content.
    media_content_in_sdp[2] = (media_content_in_sdp[0] + (i + 1) % 2 + 1) % 3;

    std::string sdp_string = kSdpSessionString;
    for (size_t j = 0; j < 3; ++j)
      sdp_string += media_content_sdps[media_content_in_sdp[j]];

    std::unique_ptr<SessionDescriptionInterface> jdesc =
        SdpDeserialize(sdp_string);
    ASSERT_THAT(jdesc, NotNull());
    SessionDescription* desc = jdesc->description();
    EXPECT_EQ(3u, desc->contents().size());

    for (size_t j = 0; j < 3; ++j) {
      const MediaContentDescription* mdesc =
          desc->contents()[j].media_description();
      EXPECT_EQ(media_types[media_content_in_sdp[j]], mdesc->type());
    }

    std::string serialized_sdp = SdpSerialize(jdesc);
    EXPECT_EQ(sdp_string, serialized_sdp);
  }
}

TEST_F(WebRtcSdpTest, DeserializeBundleOnlyAttribute) {
  EXPECT_TRUE(CompareSessionDescription(
      MakeBundleOnlyDescription(), SdpDeserialize(kBundleOnlySdpFullString)));
}

// The semantics of "a=bundle-only" are only defined when it's used in
// combination with a 0 port on the m= line. We should ignore it if used with a
// nonzero port.
TEST_F(WebRtcSdpTest, IgnoreBundleOnlyWithNonzeroPort) {
  // Make the base bundle-only description but unset the bundle-only flag.
  std::unique_ptr<SessionDescriptionInterface> sd = MakeBundleOnlyDescription();
  sd->description()->contents()[1].bundle_only = false;

  std::string modified_sdp = kBundleOnlySdpFullString;
  Replace("m=video 0", "m=video 9", &modified_sdp);
  EXPECT_TRUE(CompareSessionDescription(sd, SdpDeserialize(modified_sdp)));
}

TEST_F(WebRtcSdpTest, SerializeBundleOnlyAttribute) {
  TestSerialize(MakeBundleOnlyDescription());
}

TEST_F(WebRtcSdpTest, DeserializePlanBSessionDescription) {
  EXPECT_TRUE(CompareSessionDescription(MakePlanBDescription(),
                                        SdpDeserialize(kPlanBSdpFullString)));
}

TEST_F(WebRtcSdpTest, SerializePlanBSessionDescription) {
  TestSerialize(MakePlanBDescription());
}

TEST_F(WebRtcSdpTest, DeserializeUnifiedPlanSessionDescription) {
  MakeUnifiedPlanDescription();
  EXPECT_TRUE(CompareSessionDescription(
      jdesc_, SdpDeserialize(kUnifiedPlanSdpFullString)));
}

TEST_F(WebRtcSdpTest, SerializeUnifiedPlanSessionDescription) {
  MakeUnifiedPlanDescription();
  TestSerialize(jdesc_);
}

// This tests deserializing a Unified Plan SDP that is compatible with both
// Unified Plan and Plan B style SDP, meaning that it contains both "a=ssrc
// msid" lines and "a=msid " lines. It tests the case for audio/video tracks
// with no stream ids and multiple stream ids. For parsing this, the Unified
// Plan a=msid lines should take priority, because the Plan B style a=ssrc msid
// lines do not support multiple stream ids and no stream ids.
TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionSpecialMsid) {
  // Create both msid lines for Plan B and Unified Plan support.
  MakeUnifiedPlanDescriptionMultipleStreamIds(kMsidSignalingMediaSection |
                                              kMsidSignalingSsrcAttribute |
                                              kMsidSignalingSemantic);

  std::unique_ptr<SessionDescriptionInterface> deserialized_description =
      SdpDeserialize(kUnifiedPlanSdpFullStringWithSpecialMsid);
  ASSERT_THAT(deserialized_description, NotNull());

  EXPECT_TRUE(CompareSessionDescription(jdesc_, deserialized_description));
  EXPECT_EQ(kMsidSignalingMediaSection | kMsidSignalingSsrcAttribute |
                kMsidSignalingSemantic,
            deserialized_description->description()->msid_signaling());
}

// Tests the serialization of a Unified Plan SDP that is compatible for both
// Unified Plan and Plan B style SDPs, meaning that it contains both "a=ssrc
// msid" lines and "a=msid " lines. It tests the case for no stream ids and
// multiple stream ids.
TEST_F(WebRtcSdpTest, SerializeSessionDescriptionSpecialMsid) {
  // Create both msid lines for Plan B and Unified Plan support.
  MakeUnifiedPlanDescriptionMultipleStreamIds(kMsidSignalingMediaSection |
                                              kMsidSignalingSsrcAttribute |
                                              kMsidSignalingSemantic);
  std::string serialized_sdp = SdpSerialize(jdesc_);
  // We explicitly test that the serialized SDP string is equal to the hard
  // coded SDP string. This is necessary, because in the parser "a=msid" lines
  // take priority over "a=ssrc msid" lines. This means if we just used
  // TestSerialize(), it could serialize an SDP that omits "a=ssrc msid" lines,
  // and still pass, because the deserialized version would be the same.
  EXPECT_EQ(kUnifiedPlanSdpFullStringWithSpecialMsid, serialized_sdp);
}

// Tests that a Unified Plan style SDP (does not contain "a=ssrc msid" lines
// that signal stream IDs) is deserialized appropriately. It tests the case for
// no stream ids and multiple stream ids.
TEST_F(WebRtcSdpTest, UnifiedPlanDeserializeSessionDescriptionSpecialMsid) {
  // Only create a=msid lines for strictly Unified Plan stream ID support.
  MakeUnifiedPlanDescriptionMultipleStreamIds(kMsidSignalingMediaSection |
                                              kMsidSignalingSemantic);

  std::string unified_plan_sdp_string =
      kUnifiedPlanSdpFullStringWithSpecialMsid;
  RemoveSsrcMsidLinesFromSdpString(&unified_plan_sdp_string);
  EXPECT_TRUE(CompareSessionDescription(
      jdesc_, SdpDeserialize(unified_plan_sdp_string)));
}

// Tests that a Unified Plan style SDP (does not contain "a=ssrc msid" lines
// that signal stream IDs) is serialized appropriately. It tests the case for no
// stream ids and multiple stream ids.
TEST_F(WebRtcSdpTest, UnifiedPlanSerializeSessionDescriptionSpecialMsid) {
  // Only create a=msid lines for strictly Unified Plan stream ID support.
  MakeUnifiedPlanDescriptionMultipleStreamIds(kMsidSignalingMediaSection |
                                              kMsidSignalingSemantic);

  TestSerialize(jdesc_);
}

// This tests that a Unified Plan SDP with no a=ssrc lines is
// serialized/deserialized appropriately. In this case the
// MediaContentDescription will contain a StreamParams object that doesn't have
// any SSRCs. Vice versa, this will be created upon deserializing an SDP with no
// SSRC lines.
TEST_F(WebRtcSdpTest, DeserializeUnifiedPlanSessionDescriptionNoSsrcSignaling) {
  MakeUnifiedPlanDescription();
  RemoveSsrcSignalingFromStreamParams();
  std::string unified_plan_sdp_string = kUnifiedPlanSdpFullString;
  RemoveSsrcLinesFromSdpString(&unified_plan_sdp_string);
  EXPECT_TRUE(CompareSessionDescription(
      jdesc_, SdpDeserialize(unified_plan_sdp_string)));
}

TEST_F(WebRtcSdpTest, SerializeUnifiedPlanSessionDescriptionNoSsrcSignaling) {
  MakeUnifiedPlanDescription();
  RemoveSsrcSignalingFromStreamParams();

  TestSerialize(jdesc_);
}

TEST_F(WebRtcSdpTest, EmptyDescriptionHasNoMsidSignaling) {
  std::unique_ptr<SessionDescriptionInterface> jsep_desc =
      SdpDeserialize(kSdpSessionString);
  ASSERT_THAT(jsep_desc, NotNull());
  EXPECT_EQ(kMsidSignalingSemantic, jsep_desc->description()->msid_signaling());
}

TEST_F(WebRtcSdpTest, DataChannelOnlyHasNoMsidSignaling) {
  std::string sdp = kSdpSessionString;
  sdp += kSdpSctpDataChannelString;
  std::unique_ptr<SessionDescriptionInterface> jsep_desc = SdpDeserialize(sdp);
  ASSERT_THAT(jsep_desc, NotNull());
  EXPECT_EQ(kMsidSignalingSemantic, jsep_desc->description()->msid_signaling());
}

TEST_F(WebRtcSdpTest, PlanBHasSsrcAttributeMsidSignaling) {
  std::unique_ptr<SessionDescriptionInterface> jsep_desc =
      SdpDeserialize(kPlanBSdpFullString);
  ASSERT_THAT(jsep_desc, NotNull());
  EXPECT_EQ(kMsidSignalingSsrcAttribute | kMsidSignalingSemantic,
            jsep_desc->description()->msid_signaling());
}

TEST_F(WebRtcSdpTest, UnifiedPlanHasMediaSectionMsidSignaling) {
  std::unique_ptr<SessionDescriptionInterface> jsep_desc =
      SdpDeserialize(kUnifiedPlanSdpFullString);
  ASSERT_THAT(jsep_desc, NotNull());
  EXPECT_EQ(kMsidSignalingMediaSection | kMsidSignalingSemantic,
            jsep_desc->description()->msid_signaling());
}

TEST_F(WebRtcSdpTest, SerializeOnlyMediaSectionMsid) {
  jdesc_->description()->set_msid_signaling(kMsidSignalingMediaSection);
  std::string sdp = SdpSerialize(jdesc_);

  EXPECT_NE(std::string::npos, sdp.find(kMediaSectionMsidLine));
  EXPECT_EQ(std::string::npos, sdp.find(kSsrcAttributeMsidLine));
}

TEST_F(WebRtcSdpTest, SerializeOnlySsrcAttributeMsid) {
  jdesc_->description()->set_msid_signaling(kMsidSignalingSsrcAttribute);
  std::string sdp = SdpSerialize(jdesc_);

  EXPECT_EQ(std::string::npos, sdp.find(kMediaSectionMsidLine));
  EXPECT_NE(std::string::npos, sdp.find(kSsrcAttributeMsidLine));
}

TEST_F(WebRtcSdpTest, SerializeBothMediaSectionAndSsrcAttributeMsid) {
  jdesc_->description()->set_msid_signaling(kMsidSignalingMediaSection |
                                            kMsidSignalingSsrcAttribute);
  std::string sdp = SdpSerialize(jdesc_);

  EXPECT_NE(std::string::npos, sdp.find(kMediaSectionMsidLine));
  EXPECT_NE(std::string::npos, sdp.find(kSsrcAttributeMsidLine));
}

TEST_F(WebRtcSdpTest, SerializeWithoutMsidSemantics) {
  jdesc_->description()->set_msid_signaling(kMsidSignalingNotUsed);
  std::string sdp = SdpSerialize(jdesc_);

  EXPECT_EQ(std::string::npos, sdp.find("a=msid-semantic:"));
}

// Regression test for integer overflow bug:
// https://bugs.chromium.org/p/chromium/issues/detail?id=648071
TEST_F(WebRtcSdpTest, DeserializeLargeBandwidthLimit) {
  // Bandwidth attribute is the max signed 32-bit int, which will get
  // multiplied by 1000 and cause int overflow if not careful.
  static const char kSdpWithLargeBandwidth[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "b=AS:2147483647\r\n"
      "foo=fail\r\n";

  ExpectParseFailure(std::string(kSdpWithLargeBandwidth), "foo=fail");
}

// Similar to the above, except that negative values are illegal, not just
// error-prone as large values are.
// https://bugs.chromium.org/p/chromium/issues/detail?id=675361
TEST_F(WebRtcSdpTest, DeserializingNegativeBandwidthLimitFails) {
  static const char kSdpWithNegativeBandwidth[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 3457 RTP/SAVPF 120\r\n"
      "b=AS:-1000\r\n";

  ExpectParseFailure(std::string(kSdpWithNegativeBandwidth), "b=AS:-1000");
}

TEST_F(WebRtcSdpTest, SdpBandwidthMetrics) {
  metrics::Reset();
  auto get_sdp = [](absl::string_view value) {
    StringBuilder sb;
    sb << "v=0\r\n"
       << "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
       << "s=-\r\n"
       << "t=0 0\r\n"
       << "m=video 3457 RTP/SAVPF 120\r\n"
       << "b=AS:" << value << "\r\n";
    return sb.Release();
  };

  // This used to be kSdpBandwidthNegativeOne, but special
  // treatment is removed.
  SdpDeserialize(get_sdp("-1"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.PeerConnection.SdpBandwidth",
                                         kSdpBandwidthNegative));

  // kSdpBandwidthZero
  SdpDeserialize(get_sdp("0"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.PeerConnection.SdpBandwidth",
                                         kSdpBandwidthZero));

  // kSdpBandwidthSmall
  SdpDeserialize(get_sdp("1000"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.PeerConnection.SdpBandwidth",
                                         kSdpBandwidthSmall));

  // kSdpBandwidthLarge
  SdpDeserialize(get_sdp("3000000"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.PeerConnection.SdpBandwidth",
                                         kSdpBandwidthLarge));

  // kSdpBandwidthNegative
  SdpDeserialize(get_sdp("-1000"));
  EXPECT_METRIC_EQ(2, metrics::NumEvents("WebRTC.PeerConnection.SdpBandwidth",
                                         kSdpBandwidthNegative));

  // kSdpBandwidthParseFailure
  SdpDeserialize(get_sdp("999999999999"));
  EXPECT_METRIC_EQ(1, metrics::NumEvents("WebRTC.PeerConnection.SdpBandwidth",
                                         kSdpBandwidthParseFailure));

  EXPECT_METRIC_EQ(6,
                   metrics::NumSamples("WebRTC.PeerConnection.SdpBandwidth"));
}

// Test that "ufrag"/"pwd" in the candidate line itself are ignored, and only
// the "a=ice-ufrag"/"a=ice-pwd" attributes are used.
// Regression test for:
// https://bugs.chromium.org/p/chromium/issues/detail?id=681286
TEST_F(WebRtcSdpTest, IceCredentialsInCandidateStringIgnored) {
  // Important piece is "ufrag foo pwd bar".
  static const char kSdpWithIceCredentialsInCandidateString[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp:9 IN IP4 0.0.0.0\r\n"
      "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
      "generation 2 ufrag foo pwd bar\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(kSdpWithIceCredentialsInCandidateString);
  ASSERT_THAT(jdesc_output, NotNull());
  const IceCandidateCollection* candidates = jdesc_output->candidates(0);
  ASSERT_NE(nullptr, candidates);
  ASSERT_EQ(1U, candidates->count());
  Candidate c = candidates->at(0)->candidate();
  EXPECT_EQ("ufrag_voice", c.username());
  EXPECT_EQ("pwd_voice", c.password());
}

// Test that attribute lines "a=ice-ufrag-something"/"a=ice-pwd-something" are
// ignored, and only the "a=ice-ufrag"/"a=ice-pwd" attributes are used.
// Regression test for:
// https://bugs.chromium.org/p/webrtc/issues/detail?id=9712
TEST_F(WebRtcSdpTest, AttributeWithPartialMatchingNameIsIgnored) {
  static const char kSdpWithFooIceCredentials[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp:9 IN IP4 0.0.0.0\r\n"
      "a=ice-ufrag-something:foo\r\na=ice-pwd-something:bar\r\n"
      "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
      "generation 2\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(kSdpWithFooIceCredentials);
  ASSERT_THAT(jdesc_output, NotNull());
  const IceCandidateCollection* candidates = jdesc_output->candidates(0);
  ASSERT_NE(nullptr, candidates);
  ASSERT_EQ(1U, candidates->count());
  Candidate c = candidates->at(0)->candidate();
  EXPECT_EQ("ufrag_voice", c.username());
  EXPECT_EQ("pwd_voice", c.password());
}

// Test that SDP with an invalid port number in "a=candidate" lines is
// rejected, without crashing.
// Regression test for:
// https://bugs.chromium.org/p/chromium/issues/detail?id=677029
TEST_F(WebRtcSdpTest, DeserializeInvalidPortInCandidateAttribute) {
  static const char kSdpWithInvalidCandidatePort[] =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp:9 IN IP4 0.0.0.0\r\n"
      "a=ice-ufrag:ufrag_voice\r\na=ice-pwd:pwd_voice\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 12345678 typ host "
      "generation 2 raddr 192.168.1.1 rport 87654321\r\n";
  EXPECT_THAT(SdpDeserialize(kSdpWithInvalidCandidatePort), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithStreamIdAndTrackId) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id track_id\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 1u);
  EXPECT_EQ(stream.stream_ids()[0], "stream_id");
  EXPECT_EQ(stream.id, "track_id");
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithEmptyStreamIdAndTrackId) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:- track_id\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 0u);
  EXPECT_EQ(stream.id, "track_id");
}

// Test that "a=msid" with a missing track ID is rejected and doesn't crash.
// Regression test for:
// https://bugs.chromium.org/p/chromium/issues/detail?id=686405
TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithMissingTrackId) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id \r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutColon) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutAttributes) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithTooManySpaces) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id track_id bogus\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithDifferentTrackIds) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id track_id\r\n"
      "a=msid:stream_id2 track_id2\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutAppData) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 1u);
  EXPECT_EQ(stream.stream_ids()[0], "stream_id");
  // Track id is randomly generated.
  EXPECT_NE(stream.id, "");
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutAppDataTwoStreams) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id\r\n"
      "a=msid:stream_id2\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 2u);
  EXPECT_EQ(stream.stream_ids()[0], "stream_id");
  EXPECT_EQ(stream.stream_ids()[1], "stream_id2");
  // Track id is randomly generated.
  EXPECT_NE(stream.id, "");
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutAppDataDuplicate) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id\r\n"
      "a=msid:stream_id\r\n";

  // This is somewhat silly but accept it. Duplicates get filtered.
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 1u);
  EXPECT_EQ(stream.stream_ids()[0], "stream_id");
  // Track id is randomly generated.
  EXPECT_NE(stream.id, "");
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutAppDataMixed) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id\r\n"
      "a=msid:stream_id2 track_id\r\n";

  // Mixing the syntax like this is not a good idea but we accept it
  // and the result is the second track_id.
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 2u);
  EXPECT_EQ(stream.stream_ids()[0], "stream_id");
  EXPECT_EQ(stream.stream_ids()[1], "stream_id2");

  // Track id is taken from second line.
  EXPECT_EQ(stream.id, "track_id");
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutAppDataMixed2) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id track_id\r\n"
      "a=msid:stream_id2\r\n";

  // Mixing the syntax like this is not a good idea but we accept it
  // and the result is the second track_id.
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 2u);
  EXPECT_EQ(stream.stream_ids()[0], "stream_id");
  EXPECT_EQ(stream.stream_ids()[1], "stream_id2");

  // Track id is taken from first line.
  EXPECT_EQ(stream.id, "track_id");
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithoutAppDataMixedNoStream) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id\r\n"
      "a=msid:- track_id\r\n";

  // This is somewhat undefined behavior but accept it and expect a single
  // stream.
  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());
  auto stream = jdesc_output->description()
                    ->contents()[0]
                    .media_description()
                    ->streams()[0];
  ASSERT_EQ(stream.stream_ids().size(), 1u);
  EXPECT_EQ(stream.stream_ids()[0], "stream_id");
  EXPECT_EQ(stream.id, "track_id");
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithMissingStreamId) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid: track_id\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, DeserializeMsidAttributeWithDuplicateStreamIdAndTrackId) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "a=mid:0\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id track_id\r\n"
      "m=audio 9 RTP/SAVPF 111\r\n"
      "a=mid:1\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=msid:stream_id track_id\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

// Tests that if both session-level address and media-level address exist, use
// the media-level address.
TEST_F(WebRtcSdpTest, ParseConnectionData) {
  // Sesssion-level address.
  std::string sdp = kSdpFullString;
  InjectAfter("s=-\r\n", "c=IN IP4 192.168.0.3\r\n", &sdp);
  std::unique_ptr<SessionDescriptionInterface> jsep_desc = SdpDeserialize(sdp);
  ASSERT_THAT(jsep_desc, NotNull());

  const auto& content1 = jsep_desc->description()->contents()[0];
  EXPECT_EQ("74.125.127.126:2345",
            content1.media_description()->connection_address().ToString());
  const auto& content2 = jsep_desc->description()->contents()[1];
  EXPECT_EQ("74.125.224.39:3457",
            content2.media_description()->connection_address().ToString());
}

// Tests that the session-level connection address will be used if the media
// level-addresses are not specified.
TEST_F(WebRtcSdpTest, ParseConnectionDataSessionLevelOnly) {
  // Sesssion-level address.
  std::string sdp = kSdpString;
  InjectAfter("s=-\r\n", "c=IN IP4 192.168.0.3\r\n", &sdp);
  // Remove the media level addresses.
  Replace("c=IN IP4 0.0.0.0\r\n", "", &sdp);
  Replace("c=IN IP4 0.0.0.0\r\n", "", &sdp);
  std::unique_ptr<SessionDescriptionInterface> jsep_desc = SdpDeserialize(sdp);
  ASSERT_THAT(jsep_desc, NotNull());

  const auto& content1 = jsep_desc->description()->contents()[0];
  EXPECT_EQ("192.168.0.3:9",
            content1.media_description()->connection_address().ToString());
  const auto& content2 = jsep_desc->description()->contents()[1];
  EXPECT_EQ("192.168.0.3:9",
            content2.media_description()->connection_address().ToString());
}

TEST_F(WebRtcSdpTest, ParseConnectionDataIPv6) {
  std::string sdp = kSdpString;
  Replace("m=audio 9 RTP/SAVPF 111 103 104\r\nc=IN IP4 0.0.0.0\r\n",
          "m=audio 9 RTP/SAVPF 111 103 104\r\nc=IN IP6 "
          "2001:0db8:85a3:0000:0000:8a2e:0370:7335\r\n",
          &sdp);
  Replace("m=video 9 RTP/SAVPF 120\r\nc=IN IP4 0.0.0.0\r\n",
          "m=video 9 RTP/SAVPF 120\r\nc=IN IP6 "
          "2001:0db8:85a3:0000:0000:8a2e:0370:7336\r\n",
          &sdp);
  std::unique_ptr<SessionDescriptionInterface> jsep_desc = SdpDeserialize(sdp);
  ASSERT_THAT(jsep_desc, NotNull());
  const auto& content1 = jsep_desc->description()->contents()[0];
  EXPECT_EQ("[2001:db8:85a3::8a2e:370:7335]:9",
            content1.media_description()->connection_address().ToString());
  const auto& content2 = jsep_desc->description()->contents()[1];
  EXPECT_EQ("[2001:db8:85a3::8a2e:370:7336]:9",
            content2.media_description()->connection_address().ToString());
}

// Test that a c= line that contains a hostname connection address can be
// parsed.
TEST_F(WebRtcSdpTest, ParseConnectionDataWithHostnameConnectionAddress) {
  std::string sdp = kSdpString;
  Replace("c=IN IP4 0.0.0.0\r\n", "c=IN IP4 example.local\r\n", &sdp);
  Replace("c=IN IP4 0.0.0.0\r\n", "c=IN IP4 example.local\r\n", &sdp);
  std::unique_ptr<SessionDescriptionInterface> jsep_desc = SdpDeserialize(sdp);
  ASSERT_THAT(jsep_desc, NotNull());

  ASSERT_NE(nullptr, jsep_desc->description());
  const auto& content1 = jsep_desc->description()->contents()[0];
  EXPECT_EQ("example.local:9",
            content1.media_description()->connection_address().ToString());
  const auto& content2 = jsep_desc->description()->contents()[1];
  EXPECT_EQ("example.local:9",
            content2.media_description()->connection_address().ToString());
}

// Test that the invalid or unsupported connection data cannot be parsed.
TEST_F(WebRtcSdpTest, ParseConnectionDataFailure) {
  std::string sdp = kSdpString;
  EXPECT_THAT(SdpDeserialize(sdp), NotNull());

  // Unsupported multicast IPv4 address.
  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n", "c=IN IP4 74.125.224.39/127\r\n", &sdp);
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());

  // Unsupported multicast IPv6 address.
  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n", "c=IN IP6 ::1/3\r\n", &sdp);
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());

  // Mismatched address type.
  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n", "c=IN IP6 74.125.224.39\r\n", &sdp);
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());

  sdp = kSdpFullString;
  Replace("c=IN IP4 74.125.224.39\r\n",
          "c=IN IP4 2001:0db8:85a3:0000:0000:8a2e:0370:7334\r\n", &sdp);
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, SerializeAndDeserializeWithConnectionAddress) {
  // Serialization.
  std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());
  // Deserialization.
  std::unique_ptr<SessionDescriptionInterface> jdesc = SdpDeserialize(message);
  ASSERT_THAT(jdesc, NotNull());
  auto audio_desc = jdesc->description()
                        ->GetContentByName(kAudioContentName)
                        ->media_description();
  auto video_desc = jdesc->description()
                        ->GetContentByName(kVideoContentName)
                        ->media_description();
  EXPECT_EQ(audio_desc_->connection_address().ToString(),
            audio_desc->connection_address().ToString());
  EXPECT_EQ(video_desc_->connection_address().ToString(),
            video_desc->connection_address().ToString());
}

// RFC4566 says "If a session has no meaningful name, the value "s= " SHOULD be
// used (i.e., a single space as the session name)." So we should accept that.
TEST_F(WebRtcSdpTest, DeserializeEmptySessionName) {
  std::string sdp = kSdpString;
  Replace("s=-\r\n", "s= \r\n", &sdp);
  EXPECT_THAT(SdpDeserialize(sdp), NotNull());
}

// Simulcast malformed input test for invalid format.
TEST_F(WebRtcSdpTest, DeserializeSimulcastNegative_EmptyAttribute) {
  ExpectParseFailureWithNewLines("a=ssrc:3 cname:stream_1_cname\r\n",
                                 "a=simulcast:\r\n", "a=simulcast:");
}

// Tests that duplicate simulcast entries in the SDP triggers a parse failure.
TEST_F(WebRtcSdpTest, DeserializeSimulcastNegative_DuplicateAttribute) {
  ExpectParseFailureWithNewLines("a=ssrc:3 cname:stream_1_cname\r\n",
                                 "a=simulcast:send 1\r\na=simulcast:recv 2\r\n",
                                 "a=simulcast:");
}

// Validates that deserialization uses the a=simulcast: attribute
TEST_F(WebRtcSdpTest, TestDeserializeSimulcastAttribute) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send\r\n";
  sdp += "a=rid:2 send\r\n";
  sdp += "a=rid:3 send\r\n";
  sdp += "a=rid:4 recv\r\n";
  sdp += "a=rid:5 recv\r\n";
  sdp += "a=rid:6 recv\r\n";
  sdp += "a=simulcast:send 1,2;3 recv 4;5;6\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  EXPECT_EQ(2ul, media->simulcast_description().send_layers().size());
  EXPECT_EQ(3ul, media->simulcast_description().receive_layers().size());
  EXPECT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  CompareRidDescriptionIds(rids, {"1", "2", "3"});
}

// Validates that deserialization removes rids that do not appear in SDP
TEST_F(WebRtcSdpTest, TestDeserializeSimulcastAttributeRemovesUnknownRids) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send\r\n";
  sdp += "a=rid:3 send\r\n";
  sdp += "a=rid:4 recv\r\n";
  sdp += "a=simulcast:send 1,2;3 recv 4;5,6\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  const SimulcastDescription& simulcast = media->simulcast_description();
  EXPECT_EQ(2ul, simulcast.send_layers().size());
  EXPECT_EQ(1ul, simulcast.receive_layers().size());

  std::vector<SimulcastLayer> all_send_layers =
      simulcast.send_layers().GetAllLayers();
  EXPECT_EQ(2ul, all_send_layers.size());
  EXPECT_EQ(0,
            absl::c_count_if(all_send_layers, [](const SimulcastLayer& layer) {
              return layer.rid == "2";
            }));

  std::vector<SimulcastLayer> all_receive_layers =
      simulcast.receive_layers().GetAllLayers();
  ASSERT_EQ(1ul, all_receive_layers.size());
  EXPECT_EQ("4", all_receive_layers[0].rid);

  EXPECT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  CompareRidDescriptionIds(rids, {"1", "3"});
}

// Validates that Simulcast removes rids that appear in both send and receive.
TEST_F(WebRtcSdpTest,
       TestDeserializeSimulcastAttributeRemovesDuplicateSendReceive) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send\r\n";
  sdp += "a=rid:2 send\r\n";
  sdp += "a=rid:3 send\r\n";
  sdp += "a=rid:4 recv\r\n";
  sdp += "a=simulcast:send 1;2;3 recv 2;4\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  const SimulcastDescription& simulcast = media->simulcast_description();
  EXPECT_EQ(2ul, simulcast.send_layers().size());
  EXPECT_EQ(1ul, simulcast.receive_layers().size());
  EXPECT_EQ(2ul, simulcast.send_layers().GetAllLayers().size());
  EXPECT_EQ(1ul, simulcast.receive_layers().GetAllLayers().size());

  EXPECT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  CompareRidDescriptionIds(rids, {"1", "3"});
}

// Ignores empty rid line.
TEST_F(WebRtcSdpTest, TestDeserializeIgnoresEmptyRidLines) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send\r\n";
  sdp += "a=rid:2 send\r\n";
  sdp += "a=rid\r\n";   // Should ignore this line.
  sdp += "a=rid:\r\n";  // Should ignore this line.
  sdp += "a=simulcast:send 1;2\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  const SimulcastDescription& simulcast = media->simulcast_description();
  EXPECT_TRUE(simulcast.receive_layers().empty());
  EXPECT_EQ(2ul, simulcast.send_layers().size());
  EXPECT_EQ(2ul, simulcast.send_layers().GetAllLayers().size());

  EXPECT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  CompareRidDescriptionIds(rids, {"1", "2"});
}

// Ignores malformed rid lines.
TEST_F(WebRtcSdpTest, TestDeserializeIgnoresMalformedRidLines) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send pt=\r\n";              // Should ignore this line.
  sdp += "a=rid:2 receive\r\n";               // Should ignore this line.
  sdp += "a=rid:3 max-width=720;pt=120\r\n";  // Should ignore this line.
  sdp += "a=rid:4\r\n";                       // Should ignore this line.
  sdp += "a=rid:5 send\r\n";
  sdp += "a=simulcast:send 1,2,3;4,5\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  const SimulcastDescription& simulcast = media->simulcast_description();
  EXPECT_TRUE(simulcast.receive_layers().empty());
  EXPECT_EQ(1ul, simulcast.send_layers().size());
  EXPECT_EQ(1ul, simulcast.send_layers().GetAllLayers().size());

  EXPECT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  CompareRidDescriptionIds(rids, {"5"});
}

// Ignores codecs from RIDs where the PTs are missing from the m= section.
TEST_F(WebRtcSdpTest, TestDeserializeIgnoresInvalidPayloadTypesInRid) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send pt=121,120\r\n";  // Should remove 121 and keep 120.
  sdp += "a=rid:2 send pt=121\r\n";      // Should remove 121.
  sdp += "a=simulcast:send 1;2\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  const SimulcastDescription& simulcast = media->simulcast_description();
  EXPECT_TRUE(simulcast.receive_layers().empty());
  EXPECT_EQ(2ul, simulcast.send_layers().size());
  EXPECT_EQ(2ul, simulcast.send_layers().GetAllLayers().size());
  EXPECT_EQ("1", simulcast.send_layers()[0][0].rid);
  EXPECT_EQ(1ul, media->streams().size());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  EXPECT_EQ(2ul, rids.size());
  EXPECT_EQ("1", rids[0].rid);
  EXPECT_EQ(1ul, rids[0].codecs.size());
  EXPECT_EQ(120, rids[0].codecs[0].id);
  EXPECT_EQ("2", rids[1].rid);
  EXPECT_EQ(0ul, rids[1].codecs.size());
}

// Ignores duplicate rid lines
TEST_F(WebRtcSdpTest, TestDeserializeIgnoresDuplicateRidLines) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send\r\n";
  sdp += "a=rid:2 send\r\n";
  sdp += "a=rid:2 send\r\n";
  sdp += "a=rid:3 send\r\n";
  sdp += "a=rid:4 recv\r\n";
  sdp += "a=simulcast:send 1,2;3 recv 4\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  const SimulcastDescription& simulcast = media->simulcast_description();
  EXPECT_EQ(2ul, simulcast.send_layers().size());
  EXPECT_EQ(1ul, simulcast.receive_layers().size());
  EXPECT_EQ(2ul, simulcast.send_layers().GetAllLayers().size());
  EXPECT_EQ(1ul, simulcast.receive_layers().GetAllLayers().size());

  EXPECT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  CompareRidDescriptionIds(rids, {"1", "3"});
}

TEST_F(WebRtcSdpTest, TestDeserializeRidSendDirection) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 recv\r\n";
  sdp += "a=rid:2 recv\r\n";
  sdp += "a=simulcast:send 1;2\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_FALSE(media->HasSimulcast());
}

TEST_F(WebRtcSdpTest, TestDeserializeRidRecvDirection) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send\r\n";
  sdp += "a=rid:2 send\r\n";
  sdp += "a=simulcast:recv 1;2\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_FALSE(media->HasSimulcast());
}

TEST_F(WebRtcSdpTest, TestDeserializeIgnoresWrongRidDirectionLines) {
  std::string sdp = kUnifiedPlanSdpFullStringNoSsrc;
  sdp += "a=rid:1 send\r\n";
  sdp += "a=rid:2 send\r\n";
  sdp += "a=rid:3 send\r\n";
  sdp += "a=rid:4 recv\r\n";
  sdp += "a=rid:5 recv\r\n";
  sdp += "a=rid:6 recv\r\n";
  sdp += "a=simulcast:send 1;5;3 recv 4;2;6\r\n";
  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  const ContentInfos& contents = output->description()->contents();
  const MediaContentDescription* media = contents.back().media_description();
  EXPECT_TRUE(media->HasSimulcast());
  const SimulcastDescription& simulcast = media->simulcast_description();
  EXPECT_EQ(2ul, simulcast.send_layers().size());
  EXPECT_EQ(2ul, simulcast.receive_layers().size());
  EXPECT_EQ(2ul, simulcast.send_layers().GetAllLayers().size());
  EXPECT_EQ(2ul, simulcast.receive_layers().GetAllLayers().size());

  EXPECT_FALSE(media->streams().empty());
  const std::vector<RidDescription>& rids = media->streams()[0].rids();
  CompareRidDescriptionIds(rids, {"1", "3"});
}

// Simulcast serialization integration test.
// This test will serialize and deserialize the description and compare.
// More detailed tests for parsing simulcast can be found in
// unit tests for SdpSerializer.
TEST_F(WebRtcSdpTest, SerializeSimulcast_ComplexSerialization) {
  MakeUnifiedPlanDescription(/* use_ssrcs = */ false);
  auto description = jdesc_->description();
  auto media = description->GetContentDescriptionByName(kVideoContentName3);
  ASSERT_EQ(media->streams().size(), 1ul);
  StreamParams& send_stream = media->mutable_streams()[0];
  std::vector<RidDescription> send_rids;
  send_rids.push_back(RidDescription("1", RidDirection::kSend));
  send_rids.push_back(RidDescription("2", RidDirection::kSend));
  send_rids.push_back(RidDescription("3", RidDirection::kSend));
  send_rids.push_back(RidDescription("4", RidDirection::kSend));
  send_stream.set_rids(send_rids);
  std::vector<RidDescription> receive_rids;
  receive_rids.push_back(RidDescription("5", RidDirection::kReceive));
  receive_rids.push_back(RidDescription("6", RidDirection::kReceive));
  receive_rids.push_back(RidDescription("7", RidDirection::kReceive));
  media->set_receive_rids(receive_rids);

  SimulcastDescription& simulcast = media->simulcast_description();
  simulcast.send_layers().AddLayerWithAlternatives(
      {SimulcastLayer("2", false), SimulcastLayer("1", true)});
  simulcast.send_layers().AddLayerWithAlternatives(
      {SimulcastLayer("4", false), SimulcastLayer("3", false)});
  simulcast.receive_layers().AddLayer({SimulcastLayer("5", false)});
  simulcast.receive_layers().AddLayer({SimulcastLayer("6", false)});
  simulcast.receive_layers().AddLayer({SimulcastLayer("7", false)});

  TestSerialize(jdesc_);
}

// Test that the content name is empty if the media section does not have an
// a=mid line.
TEST_F(WebRtcSdpTest, ParseNoMid) {
  std::string sdp = kSdpString;
  Replace("a=mid:audio_content_name\r\n", "", &sdp);
  Replace("a=mid:video_content_name\r\n", "", &sdp);

  std::unique_ptr<SessionDescriptionInterface> output = SdpDeserialize(sdp);
  ASSERT_THAT(output, NotNull());
  EXPECT_THAT(output->description()->contents(),
              ElementsAre(Property("name", &ContentInfo::mid, ""),
                          Property("name", &ContentInfo::mid, "")));
}

TEST_F(WebRtcSdpTest, SerializeWithDefaultSctpProtocol) {
  AddSctpDataChannel(false);  // Don't use sctpmap
  std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());
  EXPECT_NE(std::string::npos, message.find(kMediaProtocolUdpDtlsSctp));
}

TEST_F(WebRtcSdpTest, DeserializeWithAllSctpProtocols) {
  AddSctpDataChannel(false);
  std::string protocols[] = {kMediaProtocolDtlsSctp, kMediaProtocolUdpDtlsSctp,
                             kMediaProtocolTcpDtlsSctp};
  for (const auto& protocol : protocols) {
    sctp_desc_->set_protocol(protocol);
    std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());
    EXPECT_NE(std::string::npos, message.find(protocol));
    SdpParseError error;
    EXPECT_THAT(SdpDeserialize(message, &error), NotNull());
  }
}

// According to https://tools.ietf.org/html/rfc5576#section-6.1, the CNAME
// attribute is mandatory, but we relax that restriction.
TEST_F(WebRtcSdpTest, DeserializeSessionDescriptionWithoutCname) {
  std::string sdp_without_cname = kSdpFullString;
  Replace("a=ssrc:1 cname:stream_1_cname\r\n", "", &sdp_without_cname);
  std::unique_ptr<SessionDescriptionInterface> new_jdesc =
      SdpDeserialize(sdp_without_cname);
  ASSERT_THAT(new_jdesc, NotNull());

  audio_desc_->mutable_streams()[0].cname = "";
  audio_desc_->mutable_streams()[0].ssrcs = {};
  EXPECT_TRUE(MatchesCurrentDescription(new_jdesc));
}

TEST_F(WebRtcSdpTest,
       DeserializeSdpWithUnrecognizedApplicationProtocolRejectsSection) {
  const char* unsupported_application_protocols[] = {
      "bogus/RTP/",      "RTP/SAVPF",         "DTLS/SCTP/RTP/", "DTLS/SCTPRTP/",
      "obviously-bogus", "UDP/TL/RTSP/SAVPF", "UDP/TL/RTSP/S"};

  for (auto proto : unsupported_application_protocols) {
    std::string sdp = kSdpSessionString;
    sdp.append("m=application 9 ");
    sdp.append(proto);
    sdp.append(" 101\r\n");

    std::unique_ptr<SessionDescriptionInterface> jdesc_output =
        SdpDeserialize(sdp);
    ASSERT_THAT(jdesc_output, NotNull());

    // Make sure we actually parsed a single media section
    ASSERT_EQ(1u, jdesc_output->description()->contents().size());

    // Content is not getting parsed as sctp but instead unsupported.
    EXPECT_EQ(nullptr, jdesc_output->description()
                           ->contents()[0]
                           .media_description()
                           ->as_sctp());
    EXPECT_NE(nullptr, jdesc_output->description()
                           ->contents()[0]
                           .media_description()
                           ->as_unsupported());

    // Reject the content
    EXPECT_TRUE(jdesc_output->description()->contents()[0].rejected);
  }
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithUnsupportedMediaType) {
  std::string sdp = kSdpSessionString;
  sdp +=
      "m=bogus 9 RTP/SAVPF 0 8\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=mid:bogusmid\r\n";
  sdp +=
      "m=audio/something 9 RTP/SAVPF 0 8\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=mid:somethingmid\r\n";

  std::unique_ptr<SessionDescriptionInterface> jdesc_output =
      SdpDeserialize(sdp);
  ASSERT_THAT(jdesc_output, NotNull());

  ASSERT_EQ(2u, jdesc_output->description()->contents().size());
  ASSERT_NE(nullptr, jdesc_output->description()
                         ->contents()[0]
                         .media_description()
                         ->as_unsupported());
  ASSERT_NE(nullptr, jdesc_output->description()
                         ->contents()[1]
                         .media_description()
                         ->as_unsupported());

  EXPECT_TRUE(jdesc_output->description()->contents()[0].rejected);
  EXPECT_TRUE(jdesc_output->description()->contents()[1].rejected);

  EXPECT_EQ(jdesc_output->description()->contents()[0].mid(), "bogusmid");
  EXPECT_EQ(jdesc_output->description()->contents()[1].mid(), "somethingmid");
}

TEST_F(WebRtcSdpTest, MediaTypeProtocolMismatch) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n";

  ExpectParseFailure(std::string(sdp + "m=audio 9 UDP/DTLS/SCTP 120\r\n"),
                     "m=audio");
  ExpectParseFailure(std::string(sdp + "m=video 9 UDP/DTLS/SCTP 120\r\n"),
                     "m=video");
  ExpectParseFailure(std::string(sdp + "m=video 9 SOMETHING 120\r\n"),
                     "m=video");
}

// Regression test for:
// https://bugs.chromium.org/p/chromium/issues/detail?id=1171965
TEST_F(WebRtcSdpTest, SctpPortInUnsupportedContent) {
  std::string sdp =
      "v=0\r\n"
      "o=- 18446744069414584320 18446462598732840960 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=o 1 DTLS/SCTP 5000\r\n"
      "a=sctp-port\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), NotNull());
}

TEST_F(WebRtcSdpTest, IllegalMidCharacterValue) {
  std::string sdp = kSdpString;
  // [ is an illegal token value.
  Replace("a=mid:", "a=mid:[]", &sdp);
  ExpectParseFailure(std::string(sdp), "a=mid:[]");
}

TEST_F(WebRtcSdpTest, MaxChannels) {
  std::string sdp =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 49232 RTP/AVP 108\r\n"
      "a=rtpmap:108 ISAC/16000/512\r\n";

  ExpectParseFailure(sdp, "a=rtpmap:108 ISAC/16000/512");
}

TEST_F(WebRtcSdpTest, DuplicateAudioRtpmapWithConflict) {
  std::string sdp =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 49232 RTP/AVP 108\r\n"
      "a=rtpmap:108 ISAC/16000\r\n"
      "a=rtpmap:108 G711/16000\r\n";

  ExpectParseFailure(sdp, "a=rtpmap:108 G711/16000");
}

TEST_F(WebRtcSdpTest, DuplicateVideoRtpmapWithConflict) {
  std::string sdp =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 49232 RTP/AVP 108\r\n"
      "a=rtpmap:108 VP8/90000\r\n"
      "a=rtpmap:108 VP9/90000\r\n";

  ExpectParseFailure(sdp, "a=rtpmap:108 VP9/90000");
}

TEST_F(WebRtcSdpTest, FmtpBeforeRtpMap) {
  std::string sdp =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 49232 RTP/AVP 108\r\n"
      "a=fmtp:108 profile-level=1\r\n"
      "a=rtpmap:108 VP9/90000\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), NotNull());
}

TEST_F(WebRtcSdpTest, StaticallyAssignedPayloadTypeWithDifferentCasing) {
  std::string sdp =
      "v=0\r\n"
      "o=- 11 22 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=audio 49232 RTP/AVP 18\r\n"
      // Casing differs from statically assigned type, this should
      // still be accepted.
      "a=rtpmap:18 g729/8000\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), NotNull());
}

// This tests parsing of SDP with unknown ssrc-specific attributes.
TEST_F(WebRtcSdpTest, ParseIgnoreUnknownSsrcSpecificAttribute) {
  std::string sdp = kSdpString;
  sdp += "a=ssrc:1 mslabel:something\r\n";
  ASSERT_THAT(SdpDeserialize(sdp), NotNull());
}

TEST_F(WebRtcSdpTest, ParseSessionLevelExtmapAttributes) {
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=group:BUNDLE 0\r\n"
      "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
      "a=setup:actpass\r\n"
      "a=ice-ufrag:ETEn\r\n"
      "a=ice-pwd:OtSK0WpNtpUjkY4+86js7Z/l\r\n"
      "a=extmap:3 "
      "http://www.ietf.org/id/"
      "draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
      "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendonly\r\n"
      "a=mid:0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n";
  std::unique_ptr<SessionDescriptionInterface> jdesc = SdpDeserialize(sdp);
  ASSERT_THAT(jdesc, NotNull());
  ASSERT_EQ(1u, jdesc->description()->contents().size());
  const auto content = jdesc->description()->contents()[0];
  const auto* audio_description = content.media_description();
  ASSERT_NE(audio_description, nullptr);
  const auto& extensions = audio_description->rtp_header_extensions();
  ASSERT_EQ(1u, extensions.size());
  EXPECT_EQ(extensions[0].uri,
            "http://www.ietf.org/id/"
            "draft-holmer-rmcat-transport-wide-cc-extensions-01");
  EXPECT_EQ(extensions[0].id, RtpHeaderExtensionId(3));
}

TEST_F(WebRtcSdpTest, RejectSessionLevelMediaLevelExtmapMixedUsage) {
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=group:BUNDLE 0\r\n"
      "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
      "a=setup:actpass\r\n"
      "a=ice-ufrag:ETEn\r\n"
      "a=ice-pwd:OtSK0WpNtpUjkY4+86js7Z/l\r\n"
      "a=extmap:3 "
      "http://www.ietf.org/id/"
      "draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
      "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
      "a=extmap:2 "
      "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendonly\r\n"
      "a=mid:0\r\n"
      "a=rtpmap:111 opus/48000/2\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, RejectDuplicateSsrcInSsrcGroup) {
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=group:BUNDLE 0\r\n"
      "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
      "a=setup:actpass\r\n"
      "a=ice-ufrag:ETEn\r\n"
      "a=ice-pwd:OtSK0WpNtpUjkY4+86js7Z/l\r\n"
      "m=video 9 UDP/TLS/RTP/SAVPF 96 97\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendonly\r\n"
      "a=mid:0\r\n"
      "a=rtpmap:96 VP8/90000\r\n"
      "a=rtpmap:97 rtx/90000\r\n"
      "a=fmtp:97 apt=96\r\n"
      "a=ssrc-group:FID 1234 1234\r\n"
      "a=ssrc:1234 cname:test\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, ExpectsTLineBeforeAttributeLine) {
  // https://www.rfc-editor.org/rfc/rfc4566#page-9
  // says a= attributes must come last.
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "a=thisisnottherightplace\r\n"
      "t=0 0\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), IsNull());
}

TEST_F(WebRtcSdpTest, IgnoresUnknownAttributeLines) {
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=somethingthatisnotunderstood\r\n";
  EXPECT_THAT(SdpDeserialize(sdp), NotNull());
}

TEST_F(WebRtcSdpTest, BackfillsDefaultFmtpValues) {
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=group:BUNDLE 0\r\n"
      "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
      "a=setup:actpass\r\n"
      "a=ice-ufrag:ETEn\r\n"
      "a=ice-pwd:OtSK0WpNtpUjkY4+86js7Z/l\r\n"
      "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendonly\r\n"
      "a=mid:0\r\n"
      "a=rtpmap:96 H264/90000\r\n"
      "a=rtpmap:97 VP9/90000\r\n"
      "a=rtpmap:98 AV1/90000\r\n"
      "a=rtpmap:99 H265/90000\r\n"
      "a=ssrc:1234 cname:test\r\n";
  std::unique_ptr<SessionDescriptionInterface> jdesc = SdpDeserialize(sdp);
  ASSERT_THAT(jdesc, NotNull());
  ASSERT_EQ(1u, jdesc->description()->contents().size());
  const auto content = jdesc->description()->contents()[0];
  const auto* description = content.media_description();
  ASSERT_NE(description, nullptr);
  const std::vector<Codec> codecs = description->codecs();
  ASSERT_EQ(codecs.size(), 4u);
  std::string value;

  EXPECT_EQ(codecs[0].name, "H264");
  EXPECT_TRUE(codecs[0].GetParam("packetization-mode", &value));
  EXPECT_EQ(value, "0");

  EXPECT_EQ(codecs[1].name, "VP9");
  EXPECT_TRUE(codecs[1].GetParam("profile-id", &value));
  EXPECT_EQ(value, "0");

  EXPECT_EQ(codecs[2].name, "AV1");
  EXPECT_TRUE(codecs[2].GetParam("profile", &value));
  EXPECT_EQ(value, "0");
  EXPECT_TRUE(codecs[2].GetParam("level-idx", &value));
  EXPECT_EQ(value, "5");
  EXPECT_TRUE(codecs[2].GetParam("tier", &value));
  EXPECT_EQ(value, "0");

  EXPECT_EQ(codecs[3].name, "H265");
  EXPECT_TRUE(codecs[3].GetParam("level-id", &value));
  EXPECT_EQ(value, "93");
  EXPECT_TRUE(codecs[3].GetParam("tx-mode", &value));
  EXPECT_EQ(value, "SRST");
}

TEST_F(WebRtcSdpTest, SctpProtocolWithNonApplication) {
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=group:BUNDLE 0\r\n"
      "a=fingerprint:sha-1 "
      "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n"
      "a=setup:actpass\r\n"
      "a=ice-ufrag:ETEn\r\n"
      "a=ice-pwd:OtSK0WpNtpUjkY4+86js7Z/l\r\n"
      "m=unsupported 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=mid:0\r\n"
      "a=sctp-port:5000\r\n"
      "a=max-message-size:262144\r\n";

  auto desc = CreateSessionDescription(SdpType::kOffer, sdp);
  ASSERT_NE(desc, nullptr);
  std::string serialized;
  EXPECT_TRUE(desc->ToString(&serialized));
}

TEST_F(WebRtcSdpTest, RejectsInvalidCharactersInBundleGroup) {
  std::string sdp_with_bad_bundle_tag = kSdpFullString;
  // Inject a "shrug" unicode character.
  InjectAfter(kSessionTime, "a=group:BUNDLE \u1f937\r\n",
              &sdp_with_bad_bundle_tag);
  EXPECT_THAT(SdpDeserialize(sdp_with_bad_bundle_tag), IsNull());
}

// Regression test for https://issues.chromium.org/441816631
TEST_F(WebRtcSdpTest, ShrugsOnUnknownStaticAudioCodecs) {
  // Note: this SDP is illegal, and needs improving.
  // See https://issues.webrtc.org/441854062 for details.
  std::string sdp_with_audio_codec_1 =
      "v=2\r\n"
      "o=- 1 2 3 4 5\r\n"
      "s=x\r\n"
      "t=0\r\n"
      "m=audio 0  1\r\n";
  EXPECT_TRUE(SdpDeserialize(sdp_with_audio_codec_1));
}

TEST_F(WebRtcSdpTest, DeserializeSframeAttribute) {
  std::string sdp = kSdpSessionString;
  sdp += kSdpVideoString;
  sdp += "a=sframe\r\n";

  auto jdesc = SdpDeserialize(sdp);
  ASSERT_THAT(jdesc, NotNull());
  ASSERT_EQ(1u, jdesc->description()->contents().size());
  EXPECT_TRUE(jdesc->description()
                  ->contents()[0]
                  .media_description()
                  ->sframe_enabled());
}

TEST_F(WebRtcSdpTest, DeserializeWithoutSframeAttribute) {
  std::string sdp = kSdpSessionString;
  sdp += kSdpVideoString;

  auto jdesc = SdpDeserialize(sdp);
  ASSERT_THAT(jdesc, NotNull());
  ASSERT_EQ(1u, jdesc->description()->contents().size());
  EXPECT_FALSE(jdesc->description()
                   ->contents()[0]
                   .media_description()
                   ->sframe_enabled());
}

TEST_F(WebRtcSdpTest, SerializeSframeAttribute) {
  video_desc_->set_sframe_enabled(true);
  std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());
  EXPECT_NE(std::string::npos, message.find("a=sframe\r\n"));
}

TEST_F(WebRtcSdpTest, SerializeWithoutSframeAttribute) {
  video_desc_->set_sframe_enabled(false);
  std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());
  EXPECT_EQ(std::string::npos, message.find("a=sframe"));
}

TEST_F(WebRtcSdpTest, SframeAttributeRoundTrip) {
  video_desc_->set_sframe_enabled(true);
  std::string message = SdpSerialize(MakeDescriptionWithoutCandidates());
  EXPECT_NE(std::string::npos, message.find("a=sframe\r\n"));

  auto jdesc = SdpDeserialize(message);
  ASSERT_THAT(jdesc, NotNull());
  ASSERT_EQ(2u, jdesc->description()->contents().size());
  // audio (index 0) should not have sframe, video (index 1) should.
  EXPECT_FALSE(jdesc->description()
                   ->contents()[0]
                   .media_description()
                   ->sframe_enabled());
  EXPECT_TRUE(jdesc->description()
                  ->contents()[1]
                  .media_description()
                  ->sframe_enabled());
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithZeroExtmapId) {
  std::string sdp = kSdpString;
  std::string invalid_extmap =
      "a=extmap:0 http://example.com/082005/ext.htm#ttime\r\n";
  InjectAfter("a=mid:audio_content_name\r\n", invalid_extmap, &sdp);
  SdpParseError error;
  std::unique_ptr<SessionDescriptionInterface> output =
      SdpDeserialize(sdp, &error);

  ASSERT_THAT(output, IsNull());
  EXPECT_EQ(error.line, "a=extmap:0 http://example.com/082005/ext.htm#ttime");
}

TEST_F(WebRtcSdpTest, DeserializeSdpWithTooLargeExtmapId) {
  std::string sdp = kSdpString;
  std::string invalid_extmap =
      "a=extmap:256 http://example.com/082005/ext.htm#ttime\r\n";
  InjectAfter("a=mid:audio_content_name\r\n", invalid_extmap, &sdp);
  SdpParseError error;
  std::unique_ptr<SessionDescriptionInterface> output =
      SdpDeserialize(sdp, &error);

  ASSERT_THAT(output, IsNull());
  EXPECT_EQ(error.line, "a=extmap:256 http://example.com/082005/ext.htm#ttime");
}

}  // namespace
}  // namespace webrtc
