/*
 *  Copyright (c) 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_BASE_FAKE_MEDIA_ENGINE_H_
#define MEDIA_BASE_FAKE_MEDIA_ENGINE_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_device.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_decoder.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_options.h"
#include "api/call/audio_sink.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "call/audio_state.h"
#include "media/base/audio_source.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "media/base/media_channel_impl.h"
#include "media/base/media_config.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/rtp_utils.h"
#include "media/base/stream_params.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/system/file_wrapper.h"
#include "test/create_test_field_trials.h"

namespace webrtc {

class FakeMediaEngine;
class FakeVideoEngine;
class FakeVoiceEngine;

// A common helper class that handles receiving RTP packets.
template <class Base>
class RtpReceiveChannelHelper : public Base, public MediaChannelUtil {
 public:
  explicit RtpReceiveChannelHelper(TaskQueueBase* network_thread)
      : MediaChannelUtil(network_thread),
        playout_(false),
        fail_set_recv_codecs_(false),
        transport_overhead_per_packet_(0),
        num_network_route_changes_(0) {}
  ~RtpReceiveChannelHelper() override = default;
  void EnableSframe() override {}
  const std::vector<RtpExtension>& recv_extensions() {
    return recv_extensions_;
  }
  bool playout() const { return playout_; }
  const std::list<std::string>& rtp_packets() const { return rtp_packets_; }

  bool CheckRtp(const void* data, size_t len) {
    bool success = !rtp_packets_.empty();
    if (success) {
      std::string packet = rtp_packets_.front();
      rtp_packets_.pop_front();
      success = (packet == std::string(static_cast<const char*>(data), len));
    }
    return success;
  }
  bool CheckNoRtp() { return rtp_packets_.empty(); }
  void set_fail_set_recv_codecs(bool fail) { fail_set_recv_codecs_ = fail; }
  void ResetUnsignaledRecvStream() override {}
  absl::AnyInvocable<void() &&> GetResetUnsignaledRecvStreamTask() override {
    return [this]() { ResetUnsignaledRecvStream(); };
  }
  std::optional<uint32_t> GetUnsignaledSsrc() const override {
    return std::nullopt;
  }
  void SetReceiveSsrcs_n(const flat_set<uint32_t>& ssrcs) override {
    receive_ssrcs_n_ = ssrcs;
  }
  void ClearReceiveSinks_n(
      std::optional<std::vector<uint32_t>> ssrcs) override {
    clear_receive_sinks_calls_.push_back(ssrcs);
  }
  std::vector<uint32_t> GetUnsignaledSsrcs() const override {
    return fake_unsignaled_ssrcs_;
  }
  void AddFakeUnsignaledSsrc(uint32_t ssrc) {
    fake_unsignaled_ssrcs_.push_back(ssrc);
  }
  void ClearFakeUnsignaledSsrcs() { fake_unsignaled_ssrcs_.clear(); }
  const flat_set<uint32_t>& receive_ssrcs_n() const { return receive_ssrcs_n_; }
  const std::vector<std::optional<std::vector<uint32_t>>>&
  clear_receive_sinks_calls() const {
    return clear_receive_sinks_calls_;
  }

  void OnDemuxerCriteriaUpdatePending() override {}
  void OnDemuxerCriteriaUpdateComplete() override {}

  bool AddRecvStream(const StreamParams& sp) override {
    if (absl::c_linear_search(receive_streams_, sp)) {
      return false;
    }
    receive_streams_.push_back(sp);
    rtp_receive_parameters_[sp.first_ssrc()] =
        CreateRtpParametersWithEncodings(sp);
    return true;
  }
  bool RemoveRecvStream(uint32_t ssrc) override {
    auto parameters_iterator = rtp_receive_parameters_.find(ssrc);
    if (parameters_iterator != rtp_receive_parameters_.end()) {
      rtp_receive_parameters_.erase(parameters_iterator);
    }
    return RemoveStreamBySsrc(&receive_streams_, ssrc);
  }

  RtpParameters GetRtpReceiverParameters(uint32_t ssrc) const override {
    auto parameters_iterator = rtp_receive_parameters_.find(ssrc);
    if (parameters_iterator != rtp_receive_parameters_.end()) {
      return parameters_iterator->second;
    }
    return RtpParameters();
  }
  RtpParameters GetDefaultRtpReceiveParameters() const override {
    return RtpParameters();
  }

  const std::vector<StreamParams>& recv_streams() const {
    return receive_streams_;
  }
  bool HasRecvStream(uint32_t ssrc) const {
    return GetStreamBySsrc(receive_streams_, ssrc) != nullptr;
  }

  int transport_overhead_per_packet() const {
    return transport_overhead_per_packet_;
  }

  NetworkRoute last_network_route() const { return last_network_route_; }
  int num_network_route_changes() const { return num_network_route_changes_; }
  void set_num_network_route_changes(int changes) {
    num_network_route_changes_ = changes;
  }

  void SetFrameDecryptor(uint32_t /* ssrc */,
                         scoped_refptr<FrameDecryptorInterface>
                         /* frame_decryptor */) override {}

  void SetDepacketizerToDecoderFrameTransformer(
      uint32_t /* ssrc */,
      scoped_refptr<FrameTransformerInterface> /* frame_transformer */)
      override {}

  void SetInterface(MediaChannelNetworkInterface* iface) override {
    network_interface_ = iface;
    MediaChannelUtil::SetInterface(iface);
  }

 protected:
  void set_playout(bool playout) { playout_ = playout; }
  bool SetRecvRtpHeaderExtensions(const std::vector<RtpExtension>& extensions) {
    recv_extensions_ = extensions;
    return true;
  }
  void OnPacketReceived(RtpPacketReceived packet) override {
    rtp_packets_.push_back(
        std::string(packet.Buffer().cdata<char>(), packet.size()));
  }
  bool fail_set_recv_codecs() const { return fail_set_recv_codecs_; }

 private:
  flat_set<uint32_t> receive_ssrcs_n_;
  std::vector<std::optional<std::vector<uint32_t>>> clear_receive_sinks_calls_;
  std::vector<uint32_t> fake_unsignaled_ssrcs_;

  bool playout_;
  std::vector<RtpExtension> recv_extensions_;
  std::list<std::string> rtp_packets_;
  std::vector<StreamParams> receive_streams_;
  std::map<uint32_t, RtpParameters> rtp_receive_parameters_;
  bool fail_set_recv_codecs_;
  int transport_overhead_per_packet_;
  NetworkRoute last_network_route_;
  int num_network_route_changes_;
  MediaChannelNetworkInterface* network_interface_ = nullptr;
};

// A common helper class that handles sending RTP packets.
template <class Base>
class RtpSendChannelHelper : public Base, public MediaChannelUtil {
 public:
  explicit RtpSendChannelHelper(TaskQueueBase* network_thread)
      : MediaChannelUtil(network_thread),
        sending_(false),
        fail_set_send_codecs_(false),
        send_ssrc_(0),
        ready_to_send_(false),
        transport_overhead_per_packet_(0),
        num_network_route_changes_(0) {}
  ~RtpSendChannelHelper() override = default;
  void EnableSframe() override {}
  const std::vector<RtpExtension>& send_extensions() {
    return send_extensions_;
  }
  bool sending() const { return sending_; }
  const std::list<std::string>& rtp_packets() const { return rtp_packets_; }

  bool SendPacket(const void* data,
                  size_t len,
                  const AsyncSocketPacketOptions& options) {
    if (!sending_) {
      return false;
    }
    CopyOnWriteBuffer packet(reinterpret_cast<const uint8_t*>(data), len,
                             kMaxRtpPacketLen);
    return MediaChannelUtil::SendPacket(&packet, options);
  }

  bool CheckRtp(const void* data, size_t len) {
    bool success = !rtp_packets_.empty();
    if (success) {
      std::string packet = rtp_packets_.front();
      rtp_packets_.pop_front();
      success = (packet == std::string(static_cast<const char*>(data), len));
    }
    return success;
  }

  bool CheckNoRtp() { return rtp_packets_.empty(); }
  void set_fail_set_send_codecs(bool fail) { fail_set_send_codecs_ = fail; }
  bool AddSendStream(const StreamParams& sp) override {
    if (absl::c_linear_search(send_streams_, sp)) {
      return false;
    }
    send_streams_.push_back(sp);
    rtp_send_parameters_[sp.first_ssrc()] =
        CreateRtpParametersWithEncodings(sp);

    if (ssrc_list_changed_callback_) {
      std::set<uint32_t> ssrcs_in_use;
      for (const auto& send_stream : send_streams_) {
        ssrcs_in_use.insert(send_stream.first_ssrc());
      }
      ssrc_list_changed_callback_(ssrcs_in_use);
    }

    return true;
  }
  bool RemoveSendStream(uint32_t ssrc) override {
    auto parameters_iterator = rtp_send_parameters_.find(ssrc);
    if (parameters_iterator != rtp_send_parameters_.end()) {
      rtp_send_parameters_.erase(parameters_iterator);
    }
    return RemoveStreamBySsrc(&send_streams_, ssrc);
  }
  void SetSsrcListChangedCallback(
      absl::AnyInvocable<void(const std::set<uint32_t>&)> callback) override {
    ssrc_list_changed_callback_ = std::move(callback);
  }

  void SetExtmapAllowMixed(bool extmap_allow_mixed) override {
    return MediaChannelUtil::SetExtmapAllowMixed(extmap_allow_mixed);
  }

  RtpParameters GetRtpSendParameters(uint32_t ssrc) const override {
    auto parameters_iterator = rtp_send_parameters_.find(ssrc);
    if (parameters_iterator != rtp_send_parameters_.end()) {
      // Take header extensions from channel global, not iterated.
      auto parameters = parameters_iterator->second;
      parameters.header_extensions = send_extensions_;
      return parameters;
    }
    return RtpParameters();
  }

  absl::AnyInvocable<RtpParameters(uint32_t)> GetRtpSendParametersCallback()
      const override {
    return [this](uint32_t ssrc) { return GetRtpSendParameters(ssrc); };
  }
  RTCError SetRtpSendParameters(uint32_t ssrc,
                                const RtpParameters& parameters,
                                SetParametersCallback callback) override {
    auto parameters_iterator = rtp_send_parameters_.find(ssrc);
    if (parameters_iterator != rtp_send_parameters_.end()) {
      auto result = CheckRtpParametersInvalidModificationAndValues(
          parameters_iterator->second, parameters, CreateTestFieldTrials());
      if (!result.ok()) {
        return InvokeSetParametersCallback(callback, result);
      }
      parameters_iterator->second = parameters;
      return InvokeSetParametersCallback(callback, RTCError::OK());
    }
    // Replicate the behavior of the real media channel: return false
    // when setting parameters for unknown SSRCs.
    return InvokeSetParametersCallback(callback,
                                       RTCError(RTCErrorType::INTERNAL_ERROR));
  }

  bool IsStreamMuted(uint32_t ssrc) const {
    bool ret = muted_streams_.find(ssrc) != muted_streams_.end();
    // If |ssrc = 0| check if the first send stream is muted.
    if (!ret && ssrc == 0 && !send_streams_.empty()) {
      return muted_streams_.find(send_streams_[0].first_ssrc()) !=
             muted_streams_.end();
    }
    return ret;
  }
  const std::vector<StreamParams>& send_streams() const {
    return send_streams_;
  }
  bool HasSendStream(uint32_t ssrc) const {
    return GetStreamBySsrc(send_streams_, ssrc) != nullptr;
  }
  // TODO(perkj): This is to support legacy unit test that only check one
  // sending stream.
  uint32_t send_ssrc() const {
    if (send_streams_.empty())
      return 0;
    return send_streams_[0].first_ssrc();
  }

  bool ready_to_send() const { return ready_to_send_; }

  int transport_overhead_per_packet() const {
    return transport_overhead_per_packet_;
  }

  NetworkRoute last_network_route() const { return last_network_route_; }
  int num_network_route_changes() const { return num_network_route_changes_; }
  void set_num_network_route_changes(int changes) {
    num_network_route_changes_ = changes;
  }

  void SetFrameEncryptor(uint32_t /* ssrc */,
                         scoped_refptr<FrameEncryptorInterface>
                         /* frame_encryptor */) override {}
  void SetSframeEncryptionConfig(
      SframeEncryptionConfig /* sframe_encryption_config */) override {}
  void SetEncoderToPacketizerFrameTransformer(
      uint32_t /* ssrc */,
      scoped_refptr<FrameTransformerInterface> /* frame_transformer */)
      override {}

  void SetInterface(MediaChannelNetworkInterface* iface) override {
    network_interface_ = iface;
    MediaChannelUtil::SetInterface(iface);
  }
  bool HasNetworkInterface() const override {
    return network_interface_ != nullptr;
  }

 protected:
  bool MuteStream(uint32_t ssrc, bool mute) {
    if (!HasSendStream(ssrc) && ssrc != 0) {
      return false;
    }
    if (mute) {
      muted_streams_.insert(ssrc);
    } else {
      muted_streams_.erase(ssrc);
    }
    return true;
  }
  bool set_sending(bool send) {
    sending_ = send;
    return true;
  }
  bool SetSendRtpHeaderExtensions(const std::vector<RtpExtension>& extensions) {
    send_extensions_ = extensions;
    return true;
  }
  void OnPacketSent(const SentPacketInfo& /* sent_packet */) override {}
  void OnReadyToSend(bool ready) override { ready_to_send_ = ready; }
  void OnNetworkRouteChanged(absl::string_view /* transport_name */,
                             const NetworkRoute& network_route) override {
    last_network_route_ = network_route;
    ++num_network_route_changes_;
    transport_overhead_per_packet_ = network_route.packet_overhead;
  }
  bool fail_set_send_codecs() const { return fail_set_send_codecs_; }

 private:
  // TODO(bugs.webrtc.org/12783): This flag is used from more than one thread.
  // As a workaround for tsan, it's currently std::atomic but that might not
  // be the appropriate fix.
  std::atomic<bool> sending_;
  std::vector<RtpExtension> send_extensions_;
  std::list<std::string> rtp_packets_;
  std::vector<StreamParams> send_streams_;
  std::set<uint32_t> muted_streams_;
  std::map<uint32_t, RtpParameters> rtp_send_parameters_;
  bool fail_set_send_codecs_;
  uint32_t send_ssrc_;
  bool ready_to_send_;
  int transport_overhead_per_packet_;
  NetworkRoute last_network_route_;
  int num_network_route_changes_;
  MediaChannelNetworkInterface* network_interface_ = nullptr;
  absl::AnyInvocable<void(const std::set<uint32_t>&)>
      ssrc_list_changed_callback_ = nullptr;
};

class FakeVoiceMediaReceiveChannel
    : public RtpReceiveChannelHelper<VoiceMediaReceiveChannelInterface> {
 public:
  struct DtmfInfo {
    DtmfInfo(uint32_t ssrc, int event_code, int duration);
    uint32_t ssrc;
    int event_code;
    int duration;
  };
  FakeVoiceMediaReceiveChannel(const AudioOptions& options,
                               TaskQueueBase* network_thread);
  ~FakeVoiceMediaReceiveChannel() override;

  // Test methods
  const std::vector<Codec>& recv_codecs() const;
  const std::vector<DtmfInfo>& dtmf_info_queue() const;
  const AudioOptions& options() const;
  int max_bps() const;
  bool HasSource(uint32_t ssrc) const;

  // Overrides
  VideoMediaReceiveChannelInterface* AsVideoReceiveChannel() override {
    return nullptr;
  }
  VoiceMediaReceiveChannelInterface* AsVoiceReceiveChannel() override {
    return this;
  }
  MediaType media_type() const override { return MediaType::AUDIO; }

  bool SetReceiverParameters(const AudioReceiverParameters& params) override;
  void SetReceive(bool receive) override;

  bool AddRecvStream(const StreamParams& sp) override;
  bool RemoveRecvStream(uint32_t ssrc) override;

  bool SetOutputVolume(uint32_t ssrc, double volume) override;
  bool SetDefaultOutputVolume(double volume) override;

  bool GetOutputVolume(uint32_t ssrc, double* volume);

  bool SetBaseMinimumPlayoutDelayMs(uint32_t ssrc, int delay_ms) override;
  std::optional<int> GetBaseMinimumPlayoutDelayMs(uint32_t ssrc) const override;

  bool GetStats(VoiceMediaReceiveInfo* info,
                bool get_and_clear_legacy_stats) override;
  absl::AnyInvocable<std::optional<VoiceMediaReceiveInfo>()> GetStatsTask(
      bool reset_legacy) override;

  void SetRawAudioSink(uint32_t ssrc,
                       std::unique_ptr<AudioSinkInterface> sink) override;
  void SetDefaultRawAudioSink(
      std::unique_ptr<AudioSinkInterface> sink) override;

  webrtc::RtcpMode RtcpMode() const { return recv_rtcp_mode_; }
  void SetRtcpMode(webrtc::RtcpMode mode) override { recv_rtcp_mode_ = mode; }
  void SetReceiveNackEnabled(bool /* enabled */) override {}
  void SetReceiveNonSenderRttEnabled(bool /* enabled */) override {}

 private:
  class VoiceChannelAudioSink : public AudioSource::Sink {
   public:
    explicit VoiceChannelAudioSink(AudioSource* source);
    ~VoiceChannelAudioSink() override;
    void OnData(const void* audio_data,
                int bits_per_sample,
                int sample_rate,
                size_t number_of_channels,
                size_t number_of_frames,
                std::optional<int64_t> absolute_capture_timestamp_ms) override;
    void OnClose() override;
    int NumPreferredChannels() const override { return -1; }
    AudioSource* source() const;

   private:
    AudioSource* source_;
  };

  bool SetRecvCodecs(const std::vector<Codec>& codecs);
  bool SetMaxSendBandwidth(int bps);
  bool SetOptions(const AudioOptions& options);

  std::vector<Codec> recv_codecs_;
  std::map<uint32_t, double> output_scalings_;
  std::map<uint32_t, int> output_delays_;
  std::vector<DtmfInfo> dtmf_info_queue_;
  AudioOptions options_;
  std::map<uint32_t, std::unique_ptr<VoiceChannelAudioSink>> local_sinks_;
  std::unique_ptr<AudioSinkInterface> sink_;
  int max_bps_;
  webrtc::RtcpMode recv_rtcp_mode_ = RtcpMode::kCompound;
};

class FakeVoiceMediaSendChannel
    : public RtpSendChannelHelper<VoiceMediaSendChannelInterface> {
 public:
  struct DtmfInfo {
    DtmfInfo(uint32_t ssrc, int event_code, int duration);
    uint32_t ssrc;
    int event_code;
    int duration;
  };
  FakeVoiceMediaSendChannel(const AudioOptions& options,
                            TaskQueueBase* network_thread);
  ~FakeVoiceMediaSendChannel() override;

  const std::vector<Codec>& send_codecs() const;
  const std::vector<DtmfInfo>& dtmf_info_queue() const;
  const AudioOptions& options() const;
  int max_bps() const;
  const std::string& mid() const;
  bool HasSource(uint32_t ssrc) const;
  bool GetOutputVolume(uint32_t ssrc, double* volume);

  // Overrides
  VideoMediaSendChannelInterface* AsVideoSendChannel() override {
    return nullptr;
  }
  VoiceMediaSendChannelInterface* AsVoiceSendChannel() override { return this; }
  MediaType media_type() const override { return MediaType::AUDIO; }

  bool SetSenderParameters(const AudioSenderParameter& params) override;
  void SetMid(absl::string_view mid) override;
  bool SetSend(bool send) override;
  bool SetAudioSend(uint32_t ssrc,
                    bool enable,
                    const AudioOptions* options,
                    AudioSource* source) override;

  bool CanInsertDtmf() override;
  bool InsertDtmf(uint32_t ssrc, int event_code, int duration) override;
  bool SetOptions(const AudioOptions& options);

  bool SenderNackEnabled() const override { return false; }
  bool SenderNonSenderRttEnabled() const override { return false; }
  std::optional<Codec> GetSendCodec() const override;

  bool GetStats(VoiceMediaSendInfo* stats) override;
  absl::AnyInvocable<std::optional<VoiceMediaSendInfo>()> GetStatsTask()
      override;

 private:
  class VoiceChannelAudioSink : public AudioSource::Sink {
   public:
    explicit VoiceChannelAudioSink(AudioSource* source);
    ~VoiceChannelAudioSink() override;
    void OnData(const void* audio_data,
                int bits_per_sample,
                int sample_rate,
                size_t number_of_channels,
                size_t number_of_frames,
                std::optional<int64_t> absolute_capture_timestamp_ms) override;
    void OnClose() override;
    int NumPreferredChannels() const override { return -1; }
    AudioSource* source() const;

   private:
    AudioSource* source_;
  };

  bool SetSendCodecs(const std::vector<Codec>& codecs);
  bool SetMaxSendBandwidth(int bps);
  bool SetLocalSource(uint32_t ssrc, AudioSource* source);

  std::vector<Codec> send_codecs_;
  std::map<uint32_t, double> output_scalings_;
  std::map<uint32_t, int> output_delays_;
  std::vector<DtmfInfo> dtmf_info_queue_;
  AudioOptions options_;
  std::map<uint32_t, std::unique_ptr<VoiceChannelAudioSink>> local_sinks_;
  int max_bps_;
  std::string mid_;
};

// A helper function to compare the FakeVoiceMediaChannel::DtmfInfo.
bool CompareDtmfInfo(const FakeVoiceMediaSendChannel::DtmfInfo& info,
                     uint32_t ssrc,
                     int event_code,
                     int duration);

class FakeVideoMediaReceiveChannel
    : public RtpReceiveChannelHelper<VideoMediaReceiveChannelInterface> {
 public:
  FakeVideoMediaReceiveChannel(const VideoOptions& /*options*/,
                               TaskQueueBase* network_thread)
      : FakeVideoMediaReceiveChannel(network_thread) {}
  explicit FakeVideoMediaReceiveChannel(TaskQueueBase* network_thread);

  ~FakeVideoMediaReceiveChannel() override;

  VideoMediaReceiveChannelInterface* AsVideoReceiveChannel() override {
    return this;
  }
  VoiceMediaReceiveChannelInterface* AsVoiceReceiveChannel() override {
    return nullptr;
  }
  MediaType media_type() const override { return MediaType::VIDEO; }

  const std::vector<Codec>& recv_codecs() const;
  const std::vector<Codec>& send_codecs() const;
  bool rendering() const;

  const std::map<uint32_t, VideoSinkInterface<VideoFrame>*>& sinks() const;
  int max_bps() const;
  bool SetReceiverParameters(const VideoReceiverParameters& params) override;

  bool SetSink(uint32_t ssrc, VideoSinkInterface<VideoFrame>* sink) override;
  void SetDefaultSink(VideoSinkInterface<VideoFrame>* sink) override;
  bool HasSink(uint32_t ssrc) const;

  void SetReceive(bool /* receive */) override {}
  void SetReceiveNonSenderRttEnabled(bool /* enabled */) override {}

  bool HasSource(uint32_t ssrc) const;
  bool AddRecvStream(const StreamParams& sp) override;
  bool RemoveRecvStream(uint32_t ssrc) override;

  bool SetBaseMinimumPlayoutDelayMs(uint32_t ssrc, int delay_ms) override;
  std::optional<int> GetBaseMinimumPlayoutDelayMs(uint32_t ssrc) const override;

  void SetRecordableEncodedFrameCallback(
      uint32_t ssrc,
      std::function<void(const RecordableEncodedFrame&)> callback) override;
  void ClearRecordableEncodedFrameCallback(uint32_t ssrc) override;
  void RequestRecvKeyFrame(uint32_t ssrc) override;
  bool GetStats(VideoMediaReceiveInfo* info) override;
  absl::AnyInvocable<std::optional<VideoMediaReceiveInfo>()> GetStatsTask()
      override;

  bool AddDefaultRecvStreamForTesting(const StreamParams& /* sp */) override {
    RTC_CHECK_NOTREACHED();
    return false;
  }

 private:
  bool SetRecvCodecs(const std::vector<Codec>& codecs);
  bool SetSendCodecs(const std::vector<Codec>& codecs);

  bool SetMaxSendBandwidth(int bps);

  std::vector<Codec> recv_codecs_;
  std::map<uint32_t, VideoSinkInterface<VideoFrame>*> sinks_;
  std::map<uint32_t, VideoSourceInterface<VideoFrame>*> sources_;
  std::map<uint32_t, int> output_delays_;

  int max_bps_;
};

class FakeVideoMediaSendChannel
    : public RtpSendChannelHelper<VideoMediaSendChannelInterface> {
 public:
  FakeVideoMediaSendChannel(const VideoOptions& options,
                            TaskQueueBase* network_thread);

  ~FakeVideoMediaSendChannel() override;

  VideoMediaSendChannelInterface* AsVideoSendChannel() override { return this; }
  VoiceMediaSendChannelInterface* AsVoiceSendChannel() override {
    return nullptr;
  }
  MediaType media_type() const override { return MediaType::VIDEO; }

  const std::vector<Codec>& send_codecs() const;
  const VideoOptions& options() const;
  const std::map<uint32_t, VideoSinkInterface<VideoFrame>*>& sinks() const;
  int max_bps() const;
  bool SetSenderParameters(const VideoSenderParameters& params) override;

  std::optional<Codec> GetSendCodec() const override;

  bool SetSend(bool send) override;
  bool SetVideoSend(uint32_t ssrc,
                    const VideoOptions* options,
                    VideoSourceInterface<VideoFrame>* source) override;

  bool HasSource(uint32_t ssrc) const;

  void FillBitrateInfo(BandwidthEstimationInfo* bwe_info) override;

  void GenerateSendKeyFrame(uint32_t ssrc,
                            const std::vector<std::string>& rids) override;
  void SetSsrcListChangedCallback(
      absl::AnyInvocable<void(const std::set<uint32_t>&)> /* callback */)
      override {}

  bool GetStats(VideoMediaSendInfo* info) override;
  absl::AnyInvocable<std::optional<VideoMediaSendInfo>()> GetStatsTask()
      override;
  bool SetOptions(const VideoOptions& options);

 private:
  bool SetSendCodecs(const std::vector<Codec>& codecs);
  bool SetMaxSendBandwidth(int bps);

  std::vector<Codec> send_codecs_;
  std::map<uint32_t, VideoSourceInterface<VideoFrame>*> sources_;
  VideoOptions options_;
  int max_bps_;
};

class FakeVoiceEngine : public VoiceEngineInterface {
 public:
  FakeVoiceEngine();
  void Init() override;
  void Terminate() override;
  scoped_refptr<AudioState> GetAudioState() const override;

  std::unique_ptr<VoiceMediaSendChannelInterface> CreateSendChannel(
      const Environment& env,
      Call* call,
      const MediaConfig& config,
      const AudioOptions& options,
      const CryptoOptions& crypto_options,
      absl::AnyInvocable<void()> parameters_changed_callback =
          nullptr) override;
  std::unique_ptr<VoiceMediaReceiveChannelInterface> CreateReceiveChannel(
      const Environment& env,
      Call* call,
      const MediaConfig& config,
      const AudioOptions& options,
      const CryptoOptions& crypto_options,
      absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet,
      absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                             const> on_frame_delivered_callback) override;

  // TODO(ossu): For proper testing, These should either individually settable
  //             or the voice engine should reference mockable factories.
  // TODO: https://issues.webrtc.org/360058654 - stop faking codecs here.
  const std::vector<Codec>& LegacySendCodecs() const override;
  const std::vector<Codec>& LegacyRecvCodecs() const override;
  const scoped_refptr<AudioEncoderFactory>& encoder_factory() const override {
    return encoder_factory_;
  }
  const scoped_refptr<AudioDecoderFactory>& decoder_factory() const override {
    return decoder_factory_;
  }
  void SetCodecs(const std::vector<Codec>& codecs);
  void SetRecvCodecs(const std::vector<Codec>& codecs);
  void SetSendCodecs(const std::vector<Codec>& codecs);
  int GetInputLevel();
  bool StartAecDump(FileWrapper file, int64_t max_size_bytes) override;
  void StopAecDump() override;
  std::optional<AudioDeviceModule::Stats> GetAudioDeviceStats() override;
  void ApplyGlobalOptions(const AudioOptions& options) override {}
  std::vector<RtpHeaderExtensionCapability> GetRtpHeaderExtensions(
      const FieldTrialsView* field_trials) const override;
  void SetRtpHeaderExtensions(
      std::vector<RtpHeaderExtensionCapability> header_extensions);

 private:
  class FakeVoiceEncoderFactory : public AudioEncoderFactory {
   public:
    explicit FakeVoiceEncoderFactory(FakeVoiceEngine* owner) : owner_(owner) {}
    std::vector<AudioCodecSpec> GetSupportedEncoders() override {
      // The reason for this convoluted mapping is because there are
      // too many tests that expect to push codecs into the fake voice
      // engine's "send_codecs/recv_codecs" and have them show up later.
      std::vector<AudioCodecSpec> specs;
      for (const auto& codec : owner_->send_codecs_) {
        size_t channels = std::max<size_t>(1, codec.channels);
        AudioCodecInfo info(codec.clockrate, channels, codec.bitrate);
        if (absl::EqualsIgnoreCase(codec.name, kOpusCodecName)) {
          info.allow_comfort_noise = true;
          info.supports_network_adaption = true;
        } else {
          info.allow_comfort_noise = false;
          info.supports_network_adaption = false;
        }
        specs.push_back(AudioCodecSpec{
            .format = {codec.name, codec.clockrate, channels, codec.params},
            .info = info});
      }
      return specs;
    }
    std::optional<AudioCodecInfo> QueryAudioEncoder(
        const SdpAudioFormat& format) override {
      return std::nullopt;
    }
    absl_nullable std::unique_ptr<AudioEncoder> Create(
        const Environment& env,
        const SdpAudioFormat& format,
        Options options) override {
      return nullptr;
    }
    FakeVoiceEngine* owner_;
  };
  class FakeVoiceDecoderFactory : public AudioDecoderFactory {
   public:
    explicit FakeVoiceDecoderFactory(FakeVoiceEngine* owner) : owner_(owner) {}
    std::vector<AudioCodecSpec> GetSupportedDecoders() override {
      // The reason for this convoluted mapping is because there are
      // too many tests that expect to push codecs into the fake voice
      // engine's "send_codecs/recv_codecs" and have them show up later.
      std::vector<AudioCodecSpec> specs;
      for (const auto& codec : owner_->recv_codecs_) {
        size_t channels = std::max<size_t>(1, codec.channels);
        AudioCodecInfo info(codec.clockrate, channels, codec.bitrate);
        if (absl::EqualsIgnoreCase(codec.name, kOpusCodecName)) {
          info.allow_comfort_noise = true;
          info.supports_network_adaption = true;
        } else {
          info.allow_comfort_noise = false;
          info.supports_network_adaption = false;
        }
        specs.push_back(AudioCodecSpec{
            .format = {codec.name, codec.clockrate, channels, codec.params},
            .info = info});
      }
      return specs;
    }
    bool IsSupportedDecoder(const SdpAudioFormat& format) override {
      return false;
    }
    absl_nullable std::unique_ptr<AudioDecoder> Create(
        const Environment& env,
        const SdpAudioFormat& format,
        std::optional<AudioCodecPairId> codec_pair_id) override {
      return nullptr;
    }

   private:
    FakeVoiceEngine* owner_;
  };

  std::vector<Codec> recv_codecs_;
  std::vector<Codec> send_codecs_;
  scoped_refptr<AudioEncoderFactory> encoder_factory_;
  scoped_refptr<AudioDecoderFactory> decoder_factory_;
  std::vector<RtpHeaderExtensionCapability> header_extensions_;

  friend class FakeMediaEngine;
};

class FakeVideoEngine : public VideoEngineInterface {
 public:
  FakeVideoEngine();
  bool SetOptions(const VideoOptions& options);
  std::unique_ptr<VideoMediaSendChannelInterface> CreateSendChannel(
      const Environment& env,
      Call* call,
      const MediaConfig& config,
      const VideoOptions& options,
      const CryptoOptions& crypto_options,
      VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
      VideoMediaSendChannelInterface::EncoderSwitchRequestCallback
          video_encoder_switch_request_callback,
      absl::AnyInvocable<void()> parameters_changed_callback) override;
  std::unique_ptr<VideoMediaReceiveChannelInterface> CreateReceiveChannel(
      const Environment& env,
      Call* call,
      const MediaConfig& config,
      const CryptoOptions& crypto_options,
      absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet,
      absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                             const> on_frame_delivered_callback) override;
  FakeVideoMediaSendChannel* GetSendChannel(size_t index);
  FakeVideoMediaReceiveChannel* GetReceiveChannel(size_t index);

  std::vector<Codec> LegacySendCodecs() const override {
    return LegacySendCodecs(true);
  }
  std::vector<Codec> LegacyRecvCodecs() const override {
    return LegacyRecvCodecs(true);
  }
  std::vector<Codec> LegacySendCodecs(bool include_rtx) const override;
  std::vector<Codec> LegacyRecvCodecs(bool include_rtx) const override;
  void SetSendCodecs(const std::vector<Codec>& codecs);
  void SetRecvCodecs(const std::vector<Codec>& codecs);
  bool SetCapture(bool capture);
  std::vector<RtpHeaderExtensionCapability> GetRtpHeaderExtensions(
      const FieldTrialsView* field_trials) const override;
  void SetRtpHeaderExtensions(
      std::vector<RtpHeaderExtensionCapability> header_extensions);

  VideoEncoderFactory* encoder_factory() const override {
    return encoder_factory_.get();
  }
  VideoDecoderFactory* decoder_factory() const override {
    return decoder_factory_.get();
  }

  std::vector<SdpVideoFormat> GetSupportedFormats(
      bool is_decoder) const override;

 private:
  class FakeVideoEncoderFactory : public VideoEncoderFactory {
   public:
    explicit FakeVideoEncoderFactory(FakeVideoEngine* owner) : owner_(owner) {}
    std::vector<SdpVideoFormat> GetSupportedFormats() const override {
      std::vector<SdpVideoFormat> formats;
      for (const auto& codec : owner_->send_codecs_) {
        SdpVideoFormat format(codec.name, codec.params);
        format.packetization = codec.packetization;
        format.tx_mode = codec.tx_mode;
        format.scalability_modes = codec.scalability_modes;
        formats.push_back(format);
      }
      return formats;
    }
    std::unique_ptr<VideoEncoder> Create(
        const Environment& env,
        const SdpVideoFormat& format) override {
      return nullptr;
    }

   private:
    const FakeVideoEngine* const owner_;
  };

  class FakeVideoDecoderFactory : public VideoDecoderFactory {
   public:
    explicit FakeVideoDecoderFactory(FakeVideoEngine* owner) : owner_(owner) {}
    std::vector<SdpVideoFormat> GetSupportedFormats() const override {
      std::vector<SdpVideoFormat> formats;
      for (const auto& codec : owner_->recv_codecs_) {
        SdpVideoFormat format(codec.name, codec.params);
        format.packetization = codec.packetization;
        format.tx_mode = codec.tx_mode;
        format.scalability_modes = codec.scalability_modes;
        formats.push_back(format);
      }
      return formats;
    }
    std::unique_ptr<VideoDecoder> Create(
        const Environment& env,
        const SdpVideoFormat& format) override {
      return nullptr;
    }

   private:
    const FakeVideoEngine* const owner_;
  };

  const std::unique_ptr<VideoEncoderFactory> encoder_factory_;
  const std::unique_ptr<VideoDecoderFactory> decoder_factory_;
  std::vector<Codec> send_codecs_;
  std::vector<Codec> recv_codecs_;
  bool capture_;
  VideoOptions options_;
  std::vector<RtpHeaderExtensionCapability> header_extensions_;

  friend class FakeMediaEngine;
};

class FakeMediaEngine : public CompositeMediaEngine {
 public:
  FakeMediaEngine();
  // Uses the given engines instead of default constructed ones, e.g.
  // subclasses that create custom media channels.
  FakeMediaEngine(std::unique_ptr<FakeVoiceEngine> voice_engine,
                  std::unique_ptr<FakeVideoEngine> video_engine);

  ~FakeMediaEngine() override;

  void SetAudioCodecs(const std::vector<Codec>& codecs);
  void SetAudioRecvCodecs(const std::vector<Codec>& codecs);
  void SetAudioSendCodecs(const std::vector<Codec>& codecs);
  void SetVideoCodecs(const std::vector<Codec>& codecs);
  void SetVideoRecvCodecs(const std::vector<Codec>& codecs);
  void SetVideoSendCodecs(const std::vector<Codec>& codecs);

  FakeVoiceEngine* fake_voice_engine() { return voice_; }
  FakeVideoEngine* fake_video_engine() { return video_; }

 private:
  FakeVoiceEngine* const voice_;
  FakeVideoEngine* const video_;
};

}  //  namespace webrtc


#endif  // MEDIA_BASE_FAKE_MEDIA_ENGINE_H_
