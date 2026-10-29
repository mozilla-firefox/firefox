/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_TEST_PEER_CONNECTION_TEST_WRAPPER_H_
#define PC_TEST_PEER_CONNECTION_TEST_WRAPPER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/data_channel_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/video/resolution.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "pc/test/fake_audio_capture_module.h"
#include "pc/test/fake_periodic_video_source.h"
#include "pc/test/fake_periodic_video_track_source.h"
#include "pc/test/fake_video_track_renderer.h"
#include "rtc_base/callback_list.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/thread.h"

namespace webrtc {

class PeerConnectionTestWrapper : public PeerConnectionObserver,
                                  public CreateSessionDescriptionObserver {
 public:
  // Asynchronously negotiates and exchanges ICE candidates between `caller` and
  // `callee`. See also WaitForNegotiation() and other "WaitFor..." methods.
  static void Connect(PeerConnectionTestWrapper* caller,
                      PeerConnectionTestWrapper* callee);
  // Synchronously negotiates. ICE candidates needs to be exchanged separately.
  static void AwaitNegotiation(PeerConnectionTestWrapper* caller,
                               PeerConnectionTestWrapper* callee);

  PeerConnectionTestWrapper(const std::string& name,
                            const Environment& env,
                            SocketServer* socket_server,
                            Thread* network_thread);
  ~PeerConnectionTestWrapper() override;

  bool CreatePc(const PeerConnectionInterface::RTCConfiguration& config,
                scoped_refptr<AudioEncoderFactory> audio_encoder_factory,
                scoped_refptr<AudioDecoderFactory> audio_decoder_factory,
                std::unique_ptr<FieldTrialsView> field_trials = nullptr);
  bool CreatePc(const PeerConnectionInterface::RTCConfiguration& config,
                scoped_refptr<AudioEncoderFactory> audio_encoder_factory,
                scoped_refptr<AudioDecoderFactory> audio_decoder_factory,
                std::unique_ptr<VideoEncoderFactory> video_encoder_factory,
                std::unique_ptr<VideoDecoderFactory> video_decoder_factory,
                std::unique_ptr<FieldTrialsView> field_trials = nullptr);

  scoped_refptr<PeerConnectionFactoryInterface> pc_factory() const {
    return peer_connection_factory_;
  }
  PeerConnectionInterface* pc() { return peer_connection_.get(); }

  scoped_refptr<DataChannelInterface> CreateDataChannel(
      const std::string& label,
      const DataChannelInit& init);

  std::optional<RtpCodecCapability> FindFirstSendCodecWithName(
      MediaType media_type,
      const std::string& name) const;

  void WaitForNegotiation();

  // Synchronous negotiation methods.
  std::unique_ptr<SessionDescriptionInterface> AwaitCreateOffer();
  std::unique_ptr<SessionDescriptionInterface> AwaitCreateAnswer();
  void AwaitSetLocalDescription(SessionDescriptionInterface* sdp);
  void AwaitSetRemoteDescription(SessionDescriptionInterface* sdp);
  // Listen for remote ICE candidates but don't add them until
  // AwaitAddRemoteIceCandidates().
  void ListenForRemoteIceCandidates(
      scoped_refptr<PeerConnectionTestWrapper> remote_wrapper);
  void AwaitAddRemoteIceCandidates();

  // Implements PeerConnectionObserver.
  void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) override;
  void OnAddTrack(
      scoped_refptr<RtpReceiverInterface> receiver,
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams) override;
  void OnDataChannel(scoped_refptr<DataChannelInterface> data_channel) override;
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) override {}
  void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const IceCandidate* candidate) override;
  void OnIceCandidateRemoved(const IceCandidate* candidate) override {}

  // Implements CreateSessionDescriptionObserver.
  void OnSuccess(SessionDescriptionInterface* desc) override;
  void OnFailure(RTCError) override {}

  void CreateOffer(
      const PeerConnectionInterface::RTCOfferAnswerOptions& options);
  void CreateAnswer(
      const PeerConnectionInterface::RTCOfferAnswerOptions& options);
  void ReceiveOfferSdp(const std::string& sdp);
  void ReceiveAnswerSdp(const std::string& sdp);
  void AddIceCandidate(const std::string& sdp_mid,
                       int sdp_mline_index,
                       const std::string& candidate);
  bool WaitForCallEstablished();
  bool WaitForConnection();
  bool WaitForAudio();
  bool WaitForVideo();
  void GetAndAddUserMedia(bool audio,
                          const AudioOptions& audio_options,
                          bool video);

  // signal callbacks
  [[deprecated]] void SubscribeOnIceCandidateReady(
      absl::AnyInvocable<void(const std::string&, int, const std::string&)>
          callback) {
    on_ice_candidate_ready_callbacks_.AddReceiver(std::move(callback));
  }
  void SubscribeOnIceCandidateReady(
      void* tag,
      absl::AnyInvocable<void(const std::string&, int, const std::string&)>
          callback) {
    on_ice_candidate_ready_callbacks_.AddReceiver(tag, std::move(callback));
  }
  void NotifyOnIceCandidateReady(const std::string& mid,
                                 int index,
                                 const std::string& candidate) {
    on_ice_candidate_ready_callbacks_.Send(mid, index, candidate);
  }
  [[deprecated]] void SubscribeOnSdpReady(
      absl::AnyInvocable<void(const std::string&)> callback) {
    on_sdp_ready_callbacks_.AddReceiver(std::move(callback));
  }
  void SubscribeOnSdpReady(
      void* tag,
      absl::AnyInvocable<void(const std::string&)> callback) {
    on_sdp_ready_callbacks_.AddReceiver(tag, std::move(callback));
  }
  void NotifyOnSdpReady(const std::string& sdp) {
    on_sdp_ready_callbacks_.Send(sdp);
  }
  [[deprecated]] void SubscribeOnDataChannel(
      absl::AnyInvocable<void(DataChannelInterface*)> callback) {
    on_data_channel_callbacks_.AddReceiver(std::move(callback));
  }
  void SubscribeOnDataChannel(
      void* tag,
      absl::AnyInvocable<void(DataChannelInterface*)> callback) {
    on_data_channel_callbacks_.AddReceiver(tag, std::move(callback));
  }
  void NotifyOnDataChannel(DataChannelInterface* channel) {
    on_data_channel_callbacks_.Send(channel);
  }

  scoped_refptr<MediaStreamInterface> GetUserMedia(
      bool audio,
      const AudioOptions& audio_options,
      bool video,
      Resolution resolution = {.width = FakePeriodicVideoSource::kDefaultWidth,
                               .height =
                                   FakePeriodicVideoSource::kDefaultHeight},
      int fps = 10);
  void StopFakeVideoSources();

 private:
  void SetLocalDescription(SdpType type, const std::string& sdp);
  void SetRemoteDescription(SdpType type, const std::string& sdp);
  bool CheckForConnection();
  bool CheckForAudio();
  bool CheckForVideo();
  void OnRemoteIceCandidate(const std::string& sdp_mid,
                            int sdp_mline_index,
                            const std::string& candidate);

  std::string name_;
  const Environment env_;
  SocketServer* const socket_server_;
  Thread* const network_thread_;
  SequenceChecker pc_thread_checker_;
  scoped_refptr<PeerConnectionInterface> peer_connection_;
  scoped_refptr<PeerConnectionFactoryInterface> peer_connection_factory_;
  scoped_refptr<FakeAudioCaptureModule> fake_audio_capture_module_;
  std::unique_ptr<FakeVideoTrackRenderer> renderer_;
  int num_get_user_media_calls_ = 0;
  bool pending_negotiation_;
  std::vector<scoped_refptr<FakePeriodicVideoTrackSource>> fake_video_sources_;
  scoped_refptr<PeerConnectionTestWrapper> remote_wrapper_;
  std::vector<std::unique_ptr<IceCandidate>> remote_ice_candidates_;

  CallbackList<const std::string&, int, const std::string&>
      on_ice_candidate_ready_callbacks_;
  CallbackList<const std::string&> on_sdp_ready_callbacks_;
  CallbackList<DataChannelInterface*> on_data_channel_callbacks_;
};

}  // namespace webrtc
#endif  // PC_TEST_PEER_CONNECTION_TEST_WRAPPER_H_
