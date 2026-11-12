/*
 *  Copyright 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_TEST_MOCK_RTP_SENDER_INTERNAL_H_
#define PC_TEST_MOCK_RTP_SENDER_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/dtmf_sender_interface.h"
#include "api/frame_transformer_interface.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "pc/rtp_sender.h"
#include "pc/scoped_operations_batcher.h"
#include "test/gmock.h"

namespace webrtc {

// The definition of MockRtpSender is copied in to avoid multiple inheritance.
class MockRtpSenderInternal : public RtpSenderInternal {
 public:
  // RtpSenderInterface methods.
  MOCK_METHOD(bool, SetTrack, (MediaStreamTrackInterface*), (override));
  MOCK_METHOD(scoped_refptr<MediaStreamTrackInterface>,
              track,
              (),
              (const, override));
  MOCK_METHOD(uint32_t, ssrc, (), (const, override));
  MOCK_METHOD(scoped_refptr<DtlsTransportInterface>,
              dtls_transport,
              (),
              (const, override));
  MOCK_METHOD(webrtc::MediaType, media_type, (), (const, override));
  MOCK_METHOD(std::string, id, (), (const, override));
  MOCK_METHOD(std::vector<std::string>, stream_ids, (), (const, override));
  MOCK_METHOD(std::vector<RtpEncodingParameters>,
              init_send_encodings,
              (),
              (const, override));
  MOCK_METHOD(void,
              set_transport,
              (webrtc::scoped_refptr<DtlsTransportInterface>),
              (override));
  MOCK_METHOD(void,
              SetCachedParameters,
              (std::optional<RtpParameters>),
              (override));
  MOCK_METHOD(RtpParameters, GetParameters, (), (const, override));
  MOCK_METHOD(RtpParameters, GetParametersInternal, (bool), (const, override));
  MOCK_METHOD(RtpParameters,
              GetParametersInternalWithAllLayers,
              (),
              (const, override));
  MOCK_METHOD(RTCError, SetParameters, (const RtpParameters&), (override));
  MOCK_METHOD(void,
              SetParametersAsync,
              (const RtpParameters&, SetParametersCallback),
              (override));
  MOCK_METHOD(RTCError,
              SetParametersInternal,
              (const RtpParameters&, SetParametersCallback, bool blocking),
              (override));
  MOCK_METHOD(RTCError,
              SetParametersInternalWithAllLayers,
              (const RtpParameters&),
              (override));
  MOCK_METHOD(void, SetSendCodecs, (std::vector<Codec>), (override));
  MOCK_METHOD(std::vector<Codec>, GetSendCodecs, (), (const, override));
  MOCK_METHOD(scoped_refptr<DtmfSenderInterface>,
              GetDtmfSender,
              (),
              (const, override));
  MOCK_METHOD(void,
              SetFrameEncryptor,
              (webrtc::scoped_refptr<FrameEncryptorInterface>),
              (override));
  MOCK_METHOD(scoped_refptr<FrameEncryptorInterface>,
              GetFrameEncryptor,
              (),
              (const, override));
  MOCK_METHOD(void,
              SetFrameTransformer,
              (webrtc::scoped_refptr<FrameTransformerInterface>),
              (override));
  MOCK_METHOD(void,
              SetEncoderSelector,
              (std::unique_ptr<VideoEncoderFactory::EncoderSelectorInterface>),
              (override));
  MOCK_METHOD(void,
              SetEncoderSelector,
              (scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>),
              (override));
  MOCK_METHOD(void, SetObserver, (RtpSenderObserverInterface*), (override));

  // RtpSenderInternal methods.
  MOCK_METHOD(void,
              SetMediaChannel,
              (webrtc::MediaSendChannelInterface*),
              (override));
  MOCK_METHOD(void, SetSsrc, (uint32_t), (override));
  MOCK_METHOD(ScopedOperationsBatcher::BatchTaskWithFinalizer,
              SetSsrcTask,
              (uint32_t, size_t),
              (override));
  MOCK_METHOD(void,
              set_stream_ids,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void, SetStreams, (const std::vector<std::string>&), (override));
  MOCK_METHOD(void,
              set_init_send_encodings,
              (const std::vector<RtpEncodingParameters>&),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(absl::AnyInvocable<void() &&>,
              DetachTrackAndGetStopTask,
              (),
              (override));
  MOCK_METHOD(int, AttachmentId, (), (const, override));
  MOCK_METHOD(RTCError,
              DisableEncodingLayers,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void, NotifyFirstPacketSent, (), (override));
  MOCK_METHOD(void, OnParametersChanged, (), (override));
};

}  // namespace webrtc

#endif  // PC_TEST_MOCK_RTP_SENDER_INTERNAL_H_
