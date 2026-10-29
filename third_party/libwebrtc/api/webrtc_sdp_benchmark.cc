/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdint>
#include <memory>
#include <string>

#include "api/jsep.h"
#include "api/webrtc_sdp.h"
#include "benchmark/registration.h"
#include "benchmark/state.h"
#include "benchmark/utils.h"
#include "rtc_base/checks.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {
namespace {

constexpr char kFingerprint[] =
    "a=fingerprint:sha-1 "
    "4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB\r\n";

void AppendBenchmarkAudioSection(int index, StringBuilder& sb) {
  sb << "m=audio 2345 UDP/TLS/RTP/SAVPF 111 63 9 0 8 13 110 126\r\n"
     << "c=IN IP4 74.125.127.126\r\n"
     << "a=rtcp:2347 IN IP4 74.125.127.126\r\n"
     << "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1234 typ host "
        "generation 2\r\n"
     << "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1235 typ host "
        "generation 2\r\n"
     << "a=candidate:a0+B/2 1 udp 2130706432 ::1 1238 typ host generation 2\r\n"
     << "a=candidate:a0+B/3 1 udp 2130706432 74.125.127.126 2345 typ srflx "
        "raddr 192.168.1.5 rport 2346 generation 2\r\n"
     << "a=ice-ufrag:ufrag_a_" << index << "\r\n"
     << "a=ice-pwd:pwd_audio_long_enough_" << index << "\r\n"
     << "a=ice-options:trickle\r\n"
     << kFingerprint << "a=setup:actpass\r\n"
     << "a=mid:audio_" << index << "\r\n"
     << "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"
     << "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/"
        "abs-send-time\r\n"
     << "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-"
        "wide-cc-extensions-01\r\n"
     << "a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid\r\n"
     << "a=sendrecv\r\n"
     << "a=msid:stream_" << index << " audio_track_" << index << "\r\n"
     << "a=rtcp-mux\r\n"
     << "a=rtcp-rsize\r\n"
     << "a=rtpmap:111 opus/48000/2\r\n"
     << "a=rtcp-fb:111 transport-cc\r\n"
     << "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
     << "a=rtpmap:63 red/48000/2\r\n"
     << "a=fmtp:63 111/111\r\n"
     << "a=rtpmap:9 G722/8000\r\n"
     << "a=rtpmap:0 PCMU/8000\r\n"
     << "a=rtpmap:8 PCMA/8000\r\n"
     << "a=rtpmap:13 CN/8000\r\n"
     << "a=rtpmap:110 telephone-event/48000\r\n"
     << "a=rtpmap:126 telephone-event/8000\r\n"
     << "a=ssrc:" << (1000 + index) << " cname:cname_a_" << index << "\r\n";
}

void AppendBenchmarkVideoCodecs(StringBuilder& sb) {
  sb << "a=rtpmap:96 VP8/90000\r\n"
     << "a=rtcp-fb:96 goog-remb\r\n"
     << "a=rtcp-fb:96 transport-cc\r\n"
     << "a=rtcp-fb:96 ccm fir\r\n"
     << "a=rtcp-fb:96 nack\r\n"
     << "a=rtcp-fb:96 nack pli\r\n"
     << "a=rtpmap:97 rtx/90000\r\n"
     << "a=fmtp:97 apt=96\r\n"
     << "a=rtpmap:98 VP9/90000\r\n"
     << "a=rtcp-fb:98 goog-remb\r\n"
     << "a=rtcp-fb:98 transport-cc\r\n"
     << "a=rtcp-fb:98 ccm fir\r\n"
     << "a=rtcp-fb:98 nack\r\n"
     << "a=rtcp-fb:98 nack pli\r\n"
     << "a=fmtp:98 profile-id=0\r\n"
     << "a=rtpmap:99 rtx/90000\r\n"
     << "a=fmtp:99 apt=98\r\n"
     << "a=rtpmap:100 VP9/90000\r\n"
     << "a=rtcp-fb:100 goog-remb\r\n"
     << "a=rtcp-fb:100 transport-cc\r\n"
     << "a=rtcp-fb:100 ccm fir\r\n"
     << "a=rtcp-fb:100 nack\r\n"
     << "a=rtcp-fb:100 nack pli\r\n"
     << "a=fmtp:100 profile-id=2\r\n"
     << "a=rtpmap:101 rtx/90000\r\n"
     << "a=fmtp:101 apt=100\r\n"
     << "a=rtpmap:102 H264/90000\r\n"
     << "a=rtcp-fb:102 goog-remb\r\n"
     << "a=rtcp-fb:102 transport-cc\r\n"
     << "a=rtcp-fb:102 ccm fir\r\n"
     << "a=rtcp-fb:102 nack\r\n"
     << "a=rtcp-fb:102 nack pli\r\n"
     << "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;"
        "profile-level-id=42001f\r\n"
     << "a=rtpmap:103 rtx/90000\r\n"
     << "a=fmtp:103 apt=102\r\n"
     << "a=rtpmap:104 AV1/90000\r\n"
     << "a=rtcp-fb:104 goog-remb\r\n"
     << "a=rtcp-fb:104 transport-cc\r\n"
     << "a=rtcp-fb:104 ccm fir\r\n"
     << "a=rtcp-fb:104 nack\r\n"
     << "a=rtcp-fb:104 nack pli\r\n"
     << "a=fmtp:104 level-idx=5;profile=0;tier=0\r\n"
     << "a=rtpmap:105 rtx/90000\r\n"
     << "a=fmtp:105 apt=104\r\n";
}

void AppendBenchmarkVideoSection(int index, StringBuilder& sb) {
  sb << "m=video 3457 UDP/TLS/RTP/SAVPF 96 97 98 99 100 101 102 103 104 105\r\n"
     << "c=IN IP4 74.125.224.39\r\n"
     << "a=rtcp:3456 IN IP4 74.125.224.39\r\n"
     << "a=candidate:a0+B/1 1 udp 2130706432 192.168.1.5 1237 typ host "
        "generation 2\r\n"
     << "a=candidate:a0+B/1 2 udp 2130706432 192.168.1.5 1236 typ host "
        "generation 2\r\n"
     << "a=candidate:a0+B/2 1 udp 2130706432 ::1 1241 typ host generation 2\r\n"
     << "a=candidate:a0+B/4 1 udp 2130706432 74.125.224.39 3457 typ relay "
        "generation 2\r\n"
     << "a=ice-ufrag:ufrag_v_" << index << "\r\n"
     << "a=ice-pwd:pwd_video_long_enough_" << index << "\r\n"
     << "a=ice-options:trickle\r\n"
     << kFingerprint << "a=setup:actpass\r\n"
     << "a=mid:video_" << index << "\r\n"
     << "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/"
        "abs-send-time\r\n"
     << "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-"
        "wide-cc-extensions-01\r\n"
     << "a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid\r\n"
     << "a=extmap:5 urn:ietf:params:rtp-hdrext:toffset\r\n"
     << "a=extmap:6 http://www.webrtc.org/experiments/rtp-hdrext/"
        "playout-delay\r\n"
     << "a=extmap:7 http://www.webrtc.org/experiments/rtp-hdrext/"
        "video-content-type\r\n"
     << "a=extmap:8 http://www.webrtc.org/experiments/rtp-hdrext/"
        "video-timing\r\n"
     << "a=extmap:9 http://www.webrtc.org/experiments/rtp-hdrext/"
        "color-space\r\n"
     << "a=extmap:10 urn:3gpp:video-orientation\r\n"
     << "a=sendrecv\r\n"
     << "a=msid:stream_" << index << " video_track_" << index << "\r\n"
     << "a=rtcp-mux\r\n"
     << "a=rtcp-rsize\r\n";
  AppendBenchmarkVideoCodecs(sb);
  uint32_t ssrc1 = 2000 + index * 2;
  uint32_t ssrc2 = ssrc1 + 1;
  sb << "a=ssrc-group:FID " << ssrc1 << " " << ssrc2 << "\r\n"
     << "a=ssrc:" << ssrc1 << " cname:cname_v_" << index << "\r\n"
     << "a=ssrc:" << ssrc2 << " cname:cname_v_" << index << "\r\n"
     << "a=rid:f send\r\n"
     << "a=rid:h send\r\n"
     << "a=rid:q send\r\n"
     << "a=simulcast:send f;h;q\r\n";
}

std::string CreateBenchmarkUnifiedPlanSdp(int num_av_pairs, bool include_data) {
  StringBuilder sb;
  sb << "v=0\r\n"
     << "o=- 18446744069414584320 2 IN IP4 127.0.0.1\r\n"
     << "s=-\r\n"
     << "t=0 0\r\n"
     << "a=extmap-allow-mixed\r\n"
     << "a=group:BUNDLE";
  for (int i = 0; i < num_av_pairs; ++i) {
    sb << " audio_" << i << " video_" << i;
  }
  if (include_data) {
    sb << " data_0";
  }
  sb << "\r\n"
     << "a=msid-semantic: WMS\r\n";
  for (int i = 0; i < num_av_pairs; ++i) {
    AppendBenchmarkAudioSection(i, sb);
    AppendBenchmarkVideoSection(i, sb);
  }
  if (include_data) {
    sb << "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:ufrag_data\r\n"
       << "a=ice-pwd:pwd_data_long_enough\r\n"
       << kFingerprint << "a=setup:actpass\r\n"
       << "a=mid:data_0\r\n"
       << "a=sctp-port:5000\r\n"
       << "a=max-message-size:262144\r\n";
  }
  return sb.Release();
}

void BM_SdpDeserializeSmallUnifiedPlan(benchmark::State& state) {
  const std::string sdp =
      CreateBenchmarkUnifiedPlanSdp(/*num_av_pairs=*/1, /*include_data=*/true);
  for (auto s : state) {
    std::unique_ptr<SessionDescriptionInterface> desc =
        SdpDeserialize(SdpType::kOffer, sdp);
    benchmark::DoNotOptimize(desc);
  }
}

void BM_SdpSerializeSmallUnifiedPlan(benchmark::State& state) {
  const std::string sdp =
      CreateBenchmarkUnifiedPlanSdp(/*num_av_pairs=*/1, /*include_data=*/true);
  std::unique_ptr<SessionDescriptionInterface> desc =
      SdpDeserialize(SdpType::kOffer, sdp);
  RTC_CHECK(desc);
  for (auto s : state) {
    std::string serialized = SdpSerialize(*desc);
    benchmark::DoNotOptimize(serialized);
  }
}

void BM_SdpDeserializeLargeUnifiedPlan(benchmark::State& state) {
  const std::string sdp = CreateBenchmarkUnifiedPlanSdp(/*num_av_pairs=*/25,
                                                        /*include_data=*/false);
  for (auto s : state) {
    std::unique_ptr<SessionDescriptionInterface> desc =
        SdpDeserialize(SdpType::kOffer, sdp);
    benchmark::DoNotOptimize(desc);
  }
}

void BM_SdpSerializeLargeUnifiedPlan(benchmark::State& state) {
  const std::string sdp = CreateBenchmarkUnifiedPlanSdp(/*num_av_pairs=*/25,
                                                        /*include_data=*/false);
  std::unique_ptr<SessionDescriptionInterface> desc =
      SdpDeserialize(SdpType::kOffer, sdp);
  RTC_CHECK(desc);
  for (auto s : state) {
    std::string serialized = SdpSerialize(*desc);
    benchmark::DoNotOptimize(serialized);
  }
}

BENCHMARK(BM_SdpDeserializeSmallUnifiedPlan);
BENCHMARK(BM_SdpSerializeSmallUnifiedPlan);
BENCHMARK(BM_SdpDeserializeLargeUnifiedPlan);
BENCHMARK(BM_SdpSerializeLargeUnifiedPlan);

}  // namespace
}  // namespace webrtc
