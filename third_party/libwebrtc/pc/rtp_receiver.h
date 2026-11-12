/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file contains classes that implement RtpReceiverInterface.
// An RtpReceiver associates a MediaStreamTrackInterface with an underlying
// transport (provided by webrtc::VoiceChannel/webrtc::VideoChannel)

#ifndef PC_RTP_RECEIVER_H_
#define PC_RTP_RECEIVER_H_

#include <stdint.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/frame_transformer_interface.h"
#include "api/media_stream_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "api/sframe/sframe_types.h"
#include "api/transport/rtp/rtp_source.h"
#include "api/units/timestamp.h"
#include "media/base/media_channel.h"
#include "modules/rtp_rtcp/source/source_tracker.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

// Internal class used by PeerConnection.
class RtpReceiverInternal : public RtpReceiverInterface {
 public:
  // Call on the signaling thread, to let the receiver know that the the
  // embedded source object should enter a stopped/ended state and the track's
  // state set to `kEnded`, a final state that cannot be reversed.
  virtual void Stop() = 0;

  // Returns the underlying MediaEngine channel associated with this receiver.
  // May return nullptr if no channel is set.
  // Must be invoked on the worker thread.
  virtual MediaReceiveChannelInterface* media_channel() const = 0;

  // Sets the underlying MediaEngine channel associated with this RtpSender.
  // A VoiceMediaChannel should be used for audio RtpSenders and
  // a VideoMediaChannel should be used for video RtpSenders.
  // NOTE:
  // * SetMediaChannel(nullptr) must be called before the media channel is
  //   destroyed.
  // * This method must be invoked on the worker thread.
  virtual void SetMediaChannel(MediaReceiveChannelInterface* media_channel) = 0;

  // Configures the RtpReceiver with the underlying media channel, with the
  // given SSRC as the stream identifier.
  // This function returns a callable object that must be invoked on the worker
  // thread by the caller to complete the operation. This is done to allow the
  // caller to batch up tasks for the worker thread.
  [[nodiscard]] virtual absl::AnyInvocable<void() &&> GetSetupForMediaChannel(
      uint32_t ssrc) = 0;

  // Configures the RtpReceiver with the underlying media channel to receive an
  // unsignaled receive stream.
  // This function returns a callable object that must be invoked on the worker
  // thread by the caller to complete the operation. This is done to allow the
  // caller to batch up tasks for the worker thread.
  [[nodiscard]] virtual absl::AnyInvocable<void() &&>
  GetSetupForUnsignaledMediaChannel() = 0;

  virtual void set_transport(
      scoped_refptr<DtlsTransportInterface> dtls_transport) = 0;
  // Worker-thread getter for the current SSRC.
  virtual std::optional<uint32_t> ssrc() const = 0;
  // Signaling-thread accessors for the SSRC. Used by RtpTransceiver on the
  // signaling thread to track and bind SSRCs without cross-thread hops.
  // Like `ssrc()`, `ssrc_s()` returns the signaled SSRC if there is one and
  // otherwise the SSRC of the most recent unsignaled stream.
  virtual std::optional<uint32_t> ssrc_s() const = 0;
  // Returns true if the receiver has been configured with a signaled SSRC via
  // `GetSetupForMediaChannel()`.
  virtual bool has_signaled_ssrc_s() const = 0;
  // Binds the receiver to the SSRC of a new unsignaled stream, replacing a
  // previously bound unsignaled SSRC. Must only be called when
  // `has_signaled_ssrc_s()` returns false.
  virtual void SetUnsignaledSsrc_s(uint32_t ssrc) = 0;

  // Call this to notify the RtpReceiver when the first packet has been received
  // on the corresponding channel.
  virtual void NotifyFirstPacketReceived(uint32_t ssrc) = 0;
  // Similar to the above but done whenever the receptive flag changed.
  virtual void NotifyFirstPacketReceivedAfterReceptiveChange(uint32_t ssrc) = 0;

  // Set the associated remote media streams for this receiver. The remote track
  // will be removed from any streams that are no longer present and added to
  // any new streams.
  virtual void set_stream_ids(std::vector<std::string> stream_ids) = 0;
  // TODO(https://crbug.com/webrtc/9480): Remove SetStreams() in favor of
  // set_stream_ids() as soon as downstream projects are no longer dependent on
  // stream objects.
  virtual void SetStreams(
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams) = 0;

  virtual void OnFrameDelivered(const RtpPacketInfos& infos,
                                Timestamp delivery_time) = 0;

  // Returns an ID that changes if the attached track changes, but
  // otherwise remains constant. Used to generate IDs for stats.
  // The special value zero means that no track is attached.
  virtual int AttachmentId() const = 0;

 protected:
  static int GenerateUniqueId();

  static std::vector<scoped_refptr<MediaStreamInterface>> CreateStreamsFromIds(
      std::span<const std::string> stream_ids);
};

class RtpReceiverBase : public RtpReceiverInternal {
 public:
  RTCErrorOr<scoped_refptr<SframeDecryptorInterface>>
  CreateSframeDecryptorOrError(
      SframeCipherSuite cipher_suite,
      SframeDecryptErrorCallback error_callback) override;

  std::optional<uint32_t> ssrc() const override;
  std::optional<uint32_t> ssrc_s() const override;
  bool has_signaled_ssrc_s() const override;
  void SetUnsignaledSsrc_s(uint32_t ssrc) override;
  void OnFrameDelivered(const RtpPacketInfos& infos,
                        Timestamp delivery_time) override;

  void SetFrameDecryptor(
      scoped_refptr<FrameDecryptorInterface> frame_decryptor) override;
  scoped_refptr<FrameDecryptorInterface> GetFrameDecryptor() const override;

  std::vector<RtpSource> GetSources() const override;

  void SetObserver(RtpReceiverObserverInterface* observer) override;

  void NotifyFirstPacketReceived(uint32_t ssrc) override;
  void NotifyFirstPacketReceivedAfterReceptiveChange(uint32_t ssrc) override;

  void SetFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;

 protected:
  RtpReceiverBase(Thread* worker_thread,
                  absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
                  Clock* clock = Clock::GetRealTimeClock());

  RTC_NO_UNIQUE_ADDRESS SequenceChecker signaling_thread_checker_;
  Thread* const worker_thread_;
  SourceTracker source_tracker_ RTC_GUARDED_BY(signaling_thread_checker_);
  std::optional<uint32_t> signaled_ssrc_ RTC_GUARDED_BY(worker_thread_);
  // Signaling thread copy of `signaled_ssrc_`.
  std::optional<uint32_t> signaled_ssrc_s_
      RTC_GUARDED_BY(signaling_thread_checker_);
  scoped_refptr<FrameDecryptorInterface> frame_decryptor_
      RTC_GUARDED_BY(worker_thread_);
  scoped_refptr<FrameTransformerInterface> frame_transformer_
      RTC_GUARDED_BY(worker_thread_);

 private:
  // SSRC of the most recent unsignaled stream that the receiver was bound to.
  // Not cleared when the receiver is set up again, since the media channel may
  // keep an existing unsignaled stream across renegotiation, in which case no
  // new first packet notification is issued for it.
  std::optional<uint32_t> unsignaled_ssrc_s_
      RTC_GUARDED_BY(signaling_thread_checker_);
  RtpReceiverObserverInterface* observer_
      RTC_GUARDED_BY(&signaling_thread_checker_) = nullptr;
  bool received_first_packet_ RTC_GUARDED_BY(&signaling_thread_checker_) =
      false;
  absl::AnyInvocable<RTCError()> enable_sframe_at_owner_
      RTC_GUARDED_BY(signaling_thread_checker_);
};

}  // namespace webrtc

#endif  // PC_RTP_RECEIVER_H_
