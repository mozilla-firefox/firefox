/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_BASE_MEDIA_CONSTANTS_H_
#define MEDIA_BASE_MEDIA_CONSTANTS_H_

#include <stddef.h>

#include "absl/strings/string_view.h"
#include "rtc_base/system/rtc_export.h"

// This file contains constants related to media.

namespace webrtc {

inline constexpr int kVideoCodecClockrate = 90'000;

inline constexpr int kVideoMtu = 1200;
inline constexpr int kVideoRtpSendBufferSize = 262'144;
inline constexpr int kVideoRtpRecvBufferSize = 1'048'576;

// Default CPU thresholds.
inline constexpr float kHighSystemCpuThreshold = 0.85f;
inline constexpr float kLowSystemCpuThreshold = 0.65f;
inline constexpr float kProcessCpuThreshold = 0.10f;

extern const char kRedCodecName[];
extern const char kUlpfecCodecName[];
extern const char kFlexfecCodecName[];

inline constexpr absl::string_view kFlexfecFmtpRepairWindow = "repair-window";

extern const char kRtxCodecName[];
inline constexpr absl::string_view kCodecParamRtxTime = "rtx-time";
extern const char kCodecParamAssociatedPayloadType[];

inline constexpr absl::string_view kCodecParamAssociatedCodecName = "acn";
inline constexpr absl::string_view kCodecParamNotInNameValueFormat = "";

extern const char kOpusCodecName[];
extern const char kL16CodecName[];
extern const char kG722CodecName[];
extern const char kPcmuCodecName[];
extern const char kPcmaCodecName[];
extern const char kCnCodecName[];
extern const char kDtmfCodecName[];

// Attribute parameters
inline constexpr absl::string_view kCodecParamPTime = "ptime";
inline constexpr absl::string_view kCodecParamMaxPTime = "maxptime";
// fmtp parameters
inline constexpr absl::string_view kCodecParamMinPTime = "minptime";
inline constexpr absl::string_view kCodecParamSPropStereo = "sprop-stereo";
extern const char kCodecParamStereo[];
extern const char kCodecParamUseInbandFec[];
extern const char kCodecParamUseDtx[];
inline constexpr absl::string_view kCodecParamCbr = "cbr";
inline constexpr absl::string_view kCodecParamMaxAverageBitrate =
    "maxaveragebitrate";
inline constexpr absl::string_view kCodecParamMaxPlaybackRate =
    "maxplaybackrate";
inline constexpr absl::string_view kCodecParamPerLayerPictureLossIndication =
    "x-google-per-layer-pli";

inline constexpr absl::string_view kParamValueTrue = "1";
// Parameters are stored as parameter/value pairs. For parameters who do not
// have a value, `kParamValueEmpty` should be used as value.
inline constexpr absl::string_view kParamValueEmpty = "";

// opus parameters.
// Default value for maxptime according to
// http://tools.ietf.org/html/draft-spittka-payload-rtp-opus-03
inline constexpr int kOpusDefaultMaxPTime = 120;
inline constexpr int kOpusDefaultPTime = 20;
inline constexpr int kOpusDefaultMinPTime = 3;
inline constexpr int kOpusDefaultSPropStereo = 0;
inline constexpr int kOpusDefaultStereo = 0;
inline constexpr int kOpusDefaultUseInbandFec = 0;
inline constexpr int kOpusDefaultUseDtx = 0;
inline constexpr int kOpusDefaultMaxPlaybackRate = 48000;

// Prefered values in this code base. Note that they may differ from the default
// values in http://tools.ietf.org/html/draft-spittka-payload-rtp-opus-03
// Only frames larger or equal to 10 ms are currently supported in this code
// base.
inline constexpr int kPreferredMaxPTime = 120;
inline constexpr int kPreferredMinPTime = 10;
inline constexpr int kPreferredSPropStereo = 0;
inline constexpr int kPreferredStereo = 0;
inline constexpr int kPreferredUseInbandFec = 0;

inline constexpr absl::string_view kPacketizationParamRaw = "raw";

// rtcp-fb messages according to RFC 4585
inline constexpr absl::string_view kRtcpFbParamNack = "nack";
inline constexpr absl::string_view kRtcpFbNackParamPli = "pli";
// rtcp-fb messages according to
// http://tools.ietf.org/html/draft-alvestrand-rmcat-remb-00
inline constexpr absl::string_view kRtcpFbParamRemb = "goog-remb";
// rtcp-fb messages according to
// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
inline constexpr absl::string_view kRtcpFbParamTransportCc = "transport-cc";
// ccm submessages according to RFC 5104
inline constexpr absl::string_view kRtcpFbParamCcm = "ccm";
inline constexpr absl::string_view kRtcpFbCcmParamFir = "fir";
// Receiver reference time report
// https://tools.ietf.org/html/rfc3611 section 4.4
inline constexpr absl::string_view kRtcpFbParamRrtr = "rrtr";
// Google specific parameters
extern const char kCodecParamMaxBitrate[];
extern const char kCodecParamMinBitrate[];
extern const char kCodecParamStartBitrate[];
inline constexpr absl::string_view kCodecParamMaxQuantization =
    "x-google-max-quantization";

extern const char kComfortNoiseCodecName[];

RTC_EXPORT extern const char kVp8CodecName[];
RTC_EXPORT extern const char kVp9CodecName[];
RTC_EXPORT extern const char kAv1CodecName[];
RTC_EXPORT extern const char kH264CodecName[];
RTC_EXPORT extern const char kH265CodecName[];

// RFC 6184 RTP Payload Format for H.264 video
RTC_EXPORT extern const char kH264FmtpProfileLevelId[];
RTC_EXPORT extern const char kH264FmtpLevelAsymmetryAllowed[];
RTC_EXPORT extern const char kH264FmtpPacketizationMode[];
inline constexpr absl::string_view kH264FmtpSpropParameterSets =
    "sprop-parameter-sets";
inline constexpr absl::string_view kH264FmtpSpsPpsIdrInKeyframe =
    "sps-pps-idr-in-keyframe";
inline constexpr absl::string_view kH264ProfileLevelConstrainedBaseline =
    "42e01f";
inline constexpr absl::string_view kH264ProfileLevelConstrainedHigh = "640c1f";

// RFC 7798 RTP Payload Format for H.265 video.
// According to RFC 7742, the sprop parameters MUST NOT be included
// in SDP generated by WebRTC, so for H.265 we don't handle them, though
// current H.264 implementation honors them when receiving
// sprop-parameter-sets in SDP.
RTC_EXPORT extern const char kH265FmtpProfileSpace[];
RTC_EXPORT extern const char kH265FmtpTierFlag[];
RTC_EXPORT extern const char kH265FmtpProfileId[];
RTC_EXPORT extern const char kH265FmtpLevelId[];
RTC_EXPORT extern const char kH265FmtpProfileCompatibilityIndicator[];
RTC_EXPORT extern const char kH265FmtpInteropConstraints[];
RTC_EXPORT extern const char kH265FmtpTxMode[];

// draft-ietf-payload-vp9
inline constexpr absl::string_view kVP9ProfileId = "profile-id";

// https://aomediacodec.github.io/av1-rtp-spec/
inline constexpr absl::string_view kAv1FmtpProfile = "profile";
inline constexpr absl::string_view kAv1FmtpLevelIdx = "level-idx";
inline constexpr absl::string_view kAv1FmtpTier = "tier";

inline constexpr int kDefaultVideoMaxFramerate = 60;
inline constexpr int kDefaultVideoMaxQpVpx = 56;
inline constexpr int kDefaultVideoMaxQpAv1 = 52;
inline constexpr int kDefaultVideoMaxQpH26x = 51;

inline constexpr size_t kConferenceMaxNumSpatialLayers = 3;
inline constexpr size_t kConferenceMaxNumTemporalLayers = 3;
inline constexpr size_t kConferenceDefaultNumTemporalLayers = 3;

inline constexpr absl::string_view kApplicationSpecificBandwidth = "AS";
inline constexpr absl::string_view kTransportSpecificBandwidth = "TIAS";

}  //  namespace webrtc


#endif  // MEDIA_BASE_MEDIA_CONSTANTS_H_
