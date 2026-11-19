/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_FRAME_TRANSFORMER_INTERFACE_H_
#define API_FRAME_TRANSFORMER_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>

#include "api/ref_count.h"
#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_frame_metadata.h"
#include "rtc_base/checks.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

struct RtpTimestampWithOffset {
  uint32_t value;
  operator uint32_t() const { return value; }  // NOLINT: explicit
};
struct RtpTimestampWithoutOffset {
  uint32_t value;
  operator uint32_t() const { return value; }  // NOLINT: explicit
};
using RtpTimestampInfo =
    std::variant<RtpTimestampWithOffset, RtpTimestampWithoutOffset>;

// Owns the frame payload data.
class TransformableFrameInterface {
 public:
  // Only a known list of internal implementations of transformable frames are
  // permitted to allow internal downcasting. This is enforced via the
  // internally-constructable Passkey.
  // TODO: bugs.webrtc.org/339815768 - Remove this passkey once the
  // downcasts are removed.
  class Passkey;
  RTC_EXPORT explicit TransformableFrameInterface(Passkey);

  TransformableFrameInterface(TransformableFrameInterface&&) = default;
  TransformableFrameInterface& operator=(TransformableFrameInterface&&) =
      default;

  virtual ~TransformableFrameInterface() = default;

  // Returns the frame payload data. The data is valid until the next non-const
  // method call.
  virtual std::span<const uint8_t> GetData() const = 0;

  // Copies `data` into the owned frame payload data.
  virtual void SetData(std::span<const uint8_t> data) = 0;

  virtual uint8_t GetPayloadType() const = 0;
  virtual bool CanSetPayloadType() const { return false; }
  virtual void SetPayloadType(uint8_t payload_type) { RTC_DCHECK_NOTREACHED(); }
  virtual uint32_t GetSsrc() const = 0;
  // The full RTP timestamp may not always be known. An example are frames not
  // associated with a sender or receiver, where the random offset for RTP
  // timestamp is not known. Use GetRtpTimestampInfo instead, which makes it
  // possible to make a distinction between these two cases.
  // TODO(https://bugs.webrtc.org/526671875): Deprecate and remove this method.
  [[deprecated("Use GetRtpTimestampInfo instead")]]
  virtual uint32_t GetTimestamp() const = 0;
  virtual void SetRTPTimestamp(uint32_t rtp_timestamp_with_offset) = 0;

  // TODO(https://bugs.webrtc.org/373365537): Remove this once its usage is
  // removed from blink.
  [[deprecated(
      "Use GetPresentationTimestamp instead")]] virtual std::optional<Timestamp>
  GetCaptureTimeIdentifier() const {
    return std::nullopt;
  }

  // TODO(https://bugs.webrtc.org/14878): Change this to pure virtual after it
  // is implemented everywhere.
  virtual std::optional<Timestamp> GetPresentationTimestamp() const {
    return std::nullopt;
  }

  enum class Direction {
    kUnknown,
    kReceiver,
    kSender,
  };
  // TODO(crbug.com/1250638): Remove this distinction between receiver and
  // sender frames to allow received frames to be directly re-transmitted on
  // other PeerConnectionss.
  virtual Direction GetDirection() const { return Direction::kUnknown; }
  virtual std::string GetMimeType() const = 0;

  // Timestamp at which the packet has been first seen on the network interface.
  // Only defined for received frames.
  virtual std::optional<Timestamp> ReceiveTime() const = 0;

  // Timestamp at which the frame was captured in the capturer system.
  // For receiver frames, the timestamp is expressed in the capturer system's
  // clock relative to the NTP epoch (January 1st 1970 00:00 UTC) and is
  // available only if the absolute capture timestamp header extension is
  // enabled.
  // For sender frames, the timestamp is expressed relative to the local
  // system clock's default epoch.
  virtual std::optional<Timestamp> CaptureTime() const = 0;
  virtual bool CanSetCaptureTime() const { return false; }
  virtual void SetCaptureTime(std::optional<Timestamp> capture_time) {
    RTC_DCHECK_NOTREACHED();
  }

  // Offset between the sender system's clock and the capturer system's clock.
  // Can be used to express the capture time in the local system's clock as
  // long as the local system can determine the offset between its local clock
  // and the sender system's clock.
  // Accessible only if the absolute capture timestamp header extension is
  // enabled.
  virtual std::optional<TimeDelta> SenderCaptureTimeOffset() const = 0;

  // Returns RtpTimestampWithOffset if the full RTP timestamp is known,
  // or RtpTimestampWithoutOffset if the offset for the RTP timestamp is not
  // known.
  virtual RtpTimestampInfo GetRtpTimestampInfo() const = 0;
};

class TransformableVideoFrameInterface : public TransformableFrameInterface {
 public:
  RTC_EXPORT explicit TransformableVideoFrameInterface(Passkey passkey);
  ~TransformableVideoFrameInterface() override = default;
  virtual bool IsKeyFrame() const = 0;
  virtual std::optional<std::string> Rid() const { return std::nullopt; }
  virtual VideoFrameMetadata Metadata() const = 0;
  virtual void SetMetadata(const VideoFrameMetadata&) = 0;
};

// Extends the TransformableFrameInterface to expose audio-specific information.
class TransformableAudioFrameInterface : public TransformableFrameInterface {
 public:
  RTC_EXPORT explicit TransformableAudioFrameInterface(Passkey passkey);
  ~TransformableAudioFrameInterface() override = default;

  virtual std::span<const uint32_t> GetContributingSources() const = 0;

  virtual const std::optional<uint16_t> SequenceNumber() const = 0;

  // TODO(crbug.com/391114797): Delete this function.
  virtual std::optional<uint64_t> AbsoluteCaptureTimestamp() const = 0;

  enum class FrameType { kEmptyFrame, kAudioFrameSpeech, kAudioFrameCN };

  // TODO(crbug.com/1456628): Change this to pure virtual after it
  // is implemented everywhere.
  virtual FrameType Type() const { return FrameType::kEmptyFrame; }

  // Audio level in -dBov. Values range from 0 to 127, representing 0 to -127
  // dBov. 127 represents digital silence. Only present on remote frames if
  // the audio level header extension was included.
  virtual std::optional<uint8_t> AudioLevel() const = 0;
  virtual bool CanSetAudioLevel() const { return false; }
  virtual void SetAudioLevel(std::optional<uint8_t> audio_level_dbov) {
    RTC_DCHECK_NOTREACHED();
  }
};

// Objects implement this interface to be notified with the transformed frame.
class TransformedFrameCallback : public RefCountInterface {
 public:
  virtual void OnTransformedFrame(
      std::unique_ptr<TransformableFrameInterface> frame) = 0;

  // Request to no longer be called on each frame, instead having frames be
  // sent directly to OnTransformedFrame without additional work.
  // TODO(crbug.com/1502781): Make pure virtual once all mocks have
  // implementations.
  virtual void StartShortCircuiting() {}

  // Reverts StartShortCircuiting(): frames are passed to the frame transformer
  // again. This allows a transform that is attached after short circuiting
  // has started to take effect. Frames that are already in flight may still
  // bypass the transformer.
  virtual void StopShortCircuiting() {}

 protected:
  ~TransformedFrameCallback() override = default;
};

// Transforms encoded frames. The transformed frame is sent in a callback using
// the TransformedFrameCallback interface (see above).
class FrameTransformerInterface : public RefCountInterface {
 public:
  // Transforms `frame` using the implementing class' processing logic.
  virtual void Transform(
      std::unique_ptr<TransformableFrameInterface> transformable_frame) = 0;

  virtual void RegisterTransformedFrameCallback(
      scoped_refptr<TransformedFrameCallback>) {}
  virtual void RegisterTransformedFrameSinkCallback(
      scoped_refptr<TransformedFrameCallback>,
      uint32_t /* ssrc */) {}
  virtual void UnregisterTransformedFrameCallback() {}
  virtual void UnregisterTransformedFrameSinkCallback(uint32_t /* ssrc */) {}

 protected:
  ~FrameTransformerInterface() override = default;
};

// An interface implemented by classes that can host a transform.
// Currently this is implemented by the RTCRtpSender and RTCRtpReceiver.
class FrameTransformerHost {
 public:
  virtual ~FrameTransformerHost() {}
  virtual void SetFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) = 0;
  // TODO: bugs.webrtc.org/15929 - To be added:
  // virtual AddIncomingMediaType(RtpCodec codec) = 0;
  // virtual AddOutgoingMediaType(RtpCodec codec) = 0;
};

//------------------------------------------------------------------------------
// Implementation details follow
//------------------------------------------------------------------------------
class TransformableFrameInterface::Passkey {
 public:
  ~Passkey() = default;

 private:
  // Explicit list of allowed internal implmentations of
  // TransformableFrameInterface.
  friend class TransformableOutgoingAudioFrame;
  friend class TransformableIncomingAudioFrame;
  friend class TransformableVideoSenderFrame;
  friend class TransformableVideoReceiverFrame;

  friend class MockTransformableFrame;
  friend class MockTransformableAudioFrame;
  friend class MockTransformableVideoFrame;
  Passkey() = default;
};

}  // namespace webrtc

#endif  // API_FRAME_TRANSFORMER_INTERFACE_H_
