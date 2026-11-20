/*
 *  Copyright (c) 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_BASE_CODEC_H_
#define MEDIA_BASE_CODEC_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "api/audio_codecs/audio_format.h"
#include "api/field_trials_view.h"
#include "api/payload_type.h"
#include "api/rtp_parameters.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "media/base/media_constants.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

class FeedbackParam {
 public:
  FeedbackParam() = default;
  FeedbackParam(absl::string_view id, absl::string_view param)
      : id_(id), param_(param) {}
  explicit FeedbackParam(absl::string_view id)
      : id_(id), param_(kParamValueEmpty) {}

  bool operator==(const FeedbackParam& other) const;
  bool operator!=(const FeedbackParam& c) const { return !(*this == c); }

  const std::string& id() const { return id_; }
  const std::string& param() const { return param_; }

 private:
  std::string id_;     // e.g. "nack", "ccm"
  std::string param_;  // e.g. "", "rpsi", "fir"
};

class FeedbackParams {
 public:
  FeedbackParams();
  ~FeedbackParams();
  bool operator==(const FeedbackParams& other) const;
  bool operator!=(const FeedbackParams& c) const { return !(*this == c); }

  bool Has(const FeedbackParam& param) const;
  void Add(const FeedbackParam& param);
  bool Remove(const FeedbackParam& param);

  void Intersect(const FeedbackParams& from);

  const std::vector<FeedbackParam>& params() const { return params_; }

 private:
  bool HasDuplicateEntries() const;

  std::vector<FeedbackParam> params_;
};
constexpr int kDefaultVideoClockRateHz = 90'000;
constexpr int kDefaultAudioClockRateHz = 8'000;

struct RTC_EXPORT Codec {
  enum class Type {
    kAudio,
    kVideo,
  };

  enum class ResiliencyType {
    kNone,
    kRed,
    kUlpfec,
    kFlexfec,
    kRtx,
  };
  // Value of "id" if it's not explicitly set. Exposed for tests.
  static const int kIdNotSet = -1;

  Type type;
  PayloadType id;
  std::string name;
  int clockrate;

  // Audio only
  // Can be used to override the target bitrate in the encoder.
  // TODO(orphis): Remove in favor of alternative APIs
  int bitrate;
  size_t channels;

  // Video only
  std::optional<std::string> packetization;
  absl::InlinedVector<ScalabilityMode, kScalabilityModeCount> scalability_modes;

  // H.265 only
  std::optional<std::string> tx_mode;

  // Non key-value parameters such as the telephone-event "0‐15" are
  // represented using an empty string as key, i.e. {"": "0-15"}.
  // The equivalent of fmtp in SDP.
  CodecParameterMap params;
  // The equivalent of rtcp-fb in SDP.
  FeedbackParams feedback_params;

  Codec(const Codec& c);
  Codec(Codec&& c);

  virtual ~Codec();

  // Indicates if this codec is compatible with the specified codec by
  // checking the assigned id and profile values for the relevant video codecs.
  // The rules for this comparison, in particular the parameters are
  // codec-specific as described in RFC 3264 6.1:
  // https://www.rfc-editor.org/rfc/rfc3264#section-6.1
  // For H.264, packetization modes will be compared.
  // If H.265 is enabled, TxModes will be compared.
  // H.264 (and H.265, if enabled) levels are not compared.
  // In all other cases, parameters do not need to match.
  // This is used in SDP offer/answer codec matching.
  bool Matches(const Codec& codec) const;

  // This is an exact match similar to what is described in
  // https://w3c.github.io/webrtc-pc/#dfn-codec-match
  // with two differences:
  // - rtx which is included in capabilities  without the apt parameter
  //   so number of channels, clock rate or the equality of the parameters
  //   are not compared.
  // - parameters is compared element-wise, not as a string comparison.
  // This method should only be used to compare input on our end to something we
  // generated, done e.g. by setCodecPreferences or setParameters.
  bool MatchesRtpCodec(const RtpCodec& capability) const;

  // Find the parameter for `key` and write the value to `out`.
  bool GetParam(absl::string_view key, std::string* out) const;
  bool GetParam(absl::string_view key, int* out) const;

  void SetParam(absl::string_view key, absl::string_view value);
  void SetParam(absl::string_view key, int value);

  // It is safe to input a non-existent parameter.
  // Returns true if the parameter existed, false if it did not exist.
  bool RemoveParam(absl::string_view key);

  bool HasFeedbackParam(const FeedbackParam& param) const;
  void AddFeedbackParam(const FeedbackParam& param);

  // Filter `this` feedbacks params such that only those shared by both `this`
  // and `other` are kept.
  void IntersectFeedbackParams(const Codec& other);

  virtual RtpCodecParameters ToCodecParameters() const;

  // The codec represent an actual media codec, and not a resiliency codec.
  bool IsMediaCodec() const;
  // The codec represent a resiliency codec such as RED, RTX or FEC variants.
  bool IsResiliencyCodec() const;
  ResiliencyType GetResiliencyType() const;

  // Validates a VideoCodec's payload type, dimensions and bitrates etc. If they
  // don't make sense (such as max < min bitrate), and error is logged and
  // ValidateCodecFormat returns false.
  bool ValidateCodecFormat() const;

  std::string ToString() const;

  // Default constructor, for initialization.
  Codec()
      : Codec(Type::kAudio,
              PayloadType::NotSet(),
              "",
              kDefaultAudioClockRateHz) {}
  Codec& operator=(const Codec& c);
  Codec& operator=(Codec&& c);

  bool operator==(const Codec& c) const;

  bool operator!=(const Codec& c) const { return !(*this == c); }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Codec& c) {
    absl::Format(&sink, "[%v:", c.id);
    switch (c.type) {
      case Codec::Type::kAudio:
        sink.Append("audio/");
        break;
      case Codec::Type::kVideo:
        sink.Append("video/");
    }
    absl::Format(&sink, "%s/%d/%d", c.name, c.clockrate, c.channels);
    if (c.packetization) {
      absl::Format(&sink, ",packetization=%s", *c.packetization);
    }
    for (const auto& [key, value] : c.params) {
      sink.Append(";");
      sink.Append(key);
      sink.Append("=");
      sink.Append(value);
    }
    sink.Append("]");
  }

 protected:
  // Creates an empty codec.
  explicit Codec(Type type);
  // Creates a codec with the given parameters.
  Codec(Type type, PayloadType id, absl::string_view name, int clockrate);
  Codec(Type type,
        PayloadType id,
        absl::string_view name,
        int clockrate,
        size_t channels);

  explicit Codec(const SdpAudioFormat& c);
  explicit Codec(SdpAudioFormat&& c);
  explicit Codec(const SdpVideoFormat& c);
  explicit Codec(SdpVideoFormat&& c);

  friend Codec CreateAudioCodec(PayloadType id,
                                absl::string_view name,
                                int clockrate,
                                size_t channels);
  friend Codec CreateAudioCodec(const SdpAudioFormat& c);
  friend Codec CreateAudioRtxCodec(PayloadType rtx_payload_type,
                                   PayloadType associated_payload_type);
  friend Codec CreateVideoCodec(PayloadType id, absl::string_view name);
  friend Codec CreateVideoCodec(const SdpVideoFormat& c);
  friend Codec CreateVideoCodec(PayloadType id, const SdpVideoFormat& sdp);
};

using Codecs = std::vector<Codec>;

Codec CreateAudioCodec(PayloadType id,
                       absl::string_view name,
                       int clockrate,
                       size_t channels);
Codec CreateAudioCodec(const SdpAudioFormat& c);
Codec CreateAudioRtxCodec(PayloadType rtx_payload_type,
                          PayloadType associated_payload_type);
Codec CreateVideoCodec(absl::string_view name);
Codec CreateVideoCodec(PayloadType id, absl::string_view name);
Codec CreateVideoCodec(const SdpVideoFormat& c);
Codec CreateVideoCodec(PayloadType id, const SdpVideoFormat& sdp);
Codec CreateVideoRtxCodec(PayloadType rtx_payload_type,
                          PayloadType associated_payload_type);

// Get the codec setting associated with `payload_type`. If there
// is no codec associated with that payload type it returns nullptr.
const Codec* FindCodecById(const std::vector<Codec>& codecs,
                           PayloadType payload_type);

bool HasNack(const Codec& codec);
bool HasRemb(const Codec& codec);
bool HasRrtr(const Codec& codec);

// Returns the first codec in `supported_codecs` that matches `codec`, or
// nullptr if no codec matches.
const Codec* FindMatchingVideoCodec(const std::vector<Codec>& supported_codecs,
                                    const Codec& codec);

// Returns all codecs in `supported_codecs` that matches `codec`.
std::vector<const Codec*> FindAllMatchingCodecs(
    const std::vector<Codec>& supported_codecs,
    const Codec& codec);

// Returns a copy of `format` modified to be H.264 Constrained Baseline Profile
// (CBP) if `format` is H.264, has a valid profile-level-id, and is not already
// CBP. Otherwise, returns std::nullopt.
RTC_EXPORT std::optional<SdpVideoFormat> CreateH264ConstrainedBaselineProfile(
    const SdpVideoFormat& format);

RTC_EXPORT void AddH264ConstrainedBaselineProfileToSupportedFormats(
    std::vector<SdpVideoFormat>* supported_formats);

// This function adds the default RTCP feedback parameters to the codec,
// based on the codec name and the active field trials.
void AddDefaultFeedbackParams(Codec* codec, const FieldTrialsView& trials);

}  // namespace webrtc


#endif  // MEDIA_BASE_CODEC_H_
