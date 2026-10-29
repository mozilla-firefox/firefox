/*
 *  Copyright 2009 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/channel.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/audio_options.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "media/base/codec.h"
#include "media/base/fake_media_engine.h"
#include "media/base/fake_rtp.h"
#include "media/base/media_channel.h"
#include "media/base/media_constants.h"
#include "media/base/rid_description.h"
#include "media/base/stream_params.h"
#include "p2p/base/candidate_pair_interface.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/dtls/fake_dtls_transport.h"
#include "p2p/test/fake_packet_transport.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/rtp_transport.h"
#include "pc/rtp_transport_internal.h"
#include "pc/session_description.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/checks.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/socket.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "rtc_base/unique_id_generator.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;

const Codec kPcmuCodec = CreateAudioCodec(0, "PCMU", 8000, 1);
const Codec kPcmaCodec = CreateAudioCodec(8, "PCMA", 8000, 1);
const Codec kH264Codec = CreateVideoCodec(97, "H264");
const Codec kH264SvcCodec = CreateVideoCodec(99, "H264-SVC");
constexpr uint32_t kSsrc1 = 0x1111;
constexpr uint32_t kSsrc2 = 0x2222;
constexpr uint32_t kSsrc3 = 0x3333;
constexpr uint32_t kSsrc4 = 0x4444;
constexpr int kAudioPts[] = {0, 8};
constexpr int kVideoPts[] = {97, 99};
constexpr char kAudioMid[] = "0";
constexpr char kVideoMid[] = "1";
enum class NetworkIsWorker { Yes, No };

template <class ChannelT,
          class MediaSendChannelT,
          class MediaReceiveChannelT,
          class MediaSendChannelInterfaceT,
          class MediaReceiveChannelInterfaceT,
          class ContentT,
          class MediaInfoT,
          class OptionsT>
class Traits {
 public:
  using Channel = ChannelT;
  using MediaSendChannel = MediaSendChannelT;
  using MediaReceiveChannel = MediaReceiveChannelT;
  using MediaSendChannelInterface = MediaSendChannelInterfaceT;
  using MediaReceiveChannelInterface = MediaReceiveChannelInterfaceT;
  using Content = ContentT;
  using MediaInfo = MediaInfoT;
  using Options = OptionsT;
};

class VoiceTraits : public Traits<BaseChannel,
                                  FakeVoiceMediaSendChannel,
                                  FakeVoiceMediaReceiveChannel,
                                  VoiceMediaSendChannelInterface,
                                  VoiceMediaReceiveChannelInterface,
                                  AudioContentDescription,
                                  VoiceMediaInfo,
                                  AudioOptions> {};

class VideoTraits : public Traits<BaseChannel,
                                  FakeVideoMediaSendChannel,
                                  FakeVideoMediaReceiveChannel,
                                  VideoMediaSendChannelInterface,
                                  VideoMediaReceiveChannelInterface,
                                  VideoContentDescription,
                                  VideoMediaInfo,
                                  VideoOptions> {};

// Base class for Voice/Video tests
template <class T>
class ChannelTest : public ::testing::Test {
 public:
  enum Flags {
    RTCP_MUX = 0x1,
    SSRC_MUX = 0x8,
    DTLS = 0x10,
    // Use BaseChannel with PacketTransportInternal rather than
    // DtlsTransportInternal.
    RAW_PACKET_TRANSPORT = 0x20,
  };

  ChannelTest(bool verify_playout,
              std::span<const uint8_t> rtp_data,
              std::span<const uint8_t> rtcp_data,
              NetworkIsWorker network_is_worker)
      : verify_playout_(verify_playout),
        rtp_packet_(rtp_data.data(), rtp_data.size()),
        rtcp_packet_(rtcp_data.data(), rtcp_data.size()) {
    if (network_is_worker == NetworkIsWorker::Yes) {
      network_thread_ = Thread::Current();
    } else {
      network_thread_keeper_ = Thread::Create();
      network_thread_keeper_->SetName("Network", nullptr);
      network_thread_ = network_thread_keeper_.get();
    }
    RTC_DCHECK(network_thread_);
  }

  ~ChannelTest() override {
    if (network_thread_) {
      SendTask(network_thread_, [this]() {
        network_thread_safety_->SetNotAlive();
        DeinitChannels();

        // Transports must be created and destroyed on the network thread.
        fake_rtp_dtls_transport1_ = nullptr;
        fake_rtcp_dtls_transport1_ = nullptr;
        fake_rtp_dtls_transport2_ = nullptr;
        fake_rtcp_dtls_transport2_ = nullptr;
        fake_rtp_packet_transport1_ = nullptr;
        fake_rtcp_packet_transport1_ = nullptr;
        fake_rtp_packet_transport2_ = nullptr;
        fake_rtcp_packet_transport2_ = nullptr;
        rtp_transport1_ = nullptr;
        rtp_transport2_ = nullptr;
        new_rtp_transport_ = nullptr;
      });
    }
  }

  void CreateChannels(int flags1, int flags2) {
    CreateChannels(std::make_unique<typename T::MediaSendChannel>(
                       typename T::Options(), network_thread_),
                   std::make_unique<typename T::MediaReceiveChannel>(
                       typename T::Options(), network_thread_),
                   std::make_unique<typename T::MediaSendChannel>(
                       typename T::Options(), network_thread_),
                   std::make_unique<typename T::MediaReceiveChannel>(
                       typename T::Options(), network_thread_),
                   flags1, flags2);
  }

  void CreateChannels(std::unique_ptr<typename T::MediaSendChannel> ch1s,
                      std::unique_ptr<typename T::MediaReceiveChannel> ch1r,
                      std::unique_ptr<typename T::MediaSendChannel> ch2s,
                      std::unique_ptr<typename T::MediaReceiveChannel> ch2r,
                      int flags1,
                      int flags2) {
    RTC_DCHECK(!channel1_);
    RTC_DCHECK(!channel2_);

    // Network thread is started in CreateChannels, to allow the test to
    // configure a fake clock before any threads are spawned and attempt to
    // access the time.
    if (network_thread_keeper_) {
      network_thread_keeper_->Start();
    }

    // Make sure if using raw packet transports, they're used for both
    // channels.
    RTC_DCHECK_EQ(flags1 & RAW_PACKET_TRANSPORT, flags2 & RAW_PACKET_TRANSPORT);
    Thread* worker_thread = Thread::Current();

    network_thread_->BlockingCall([&] {
      // Based on flags, create fake DTLS or raw packet transports.

      if (flags1 & RAW_PACKET_TRANSPORT) {
        fake_rtp_packet_transport1_ =
            std::make_unique<FakePacketTransport>(env_, "channel1_rtp");
        if (!(flags1 & RTCP_MUX)) {
          fake_rtcp_packet_transport1_ =
              std::make_unique<FakePacketTransport>(env_, "channel1_rtcp");
        }
      } else {
        // Confirmed to work with KT_RSA and KT_ECDSA.
        fake_rtp_dtls_transport1_ = std::make_unique<FakeDtlsTransport>(
            env_, "channel1", ICE_CANDIDATE_COMPONENT_RTP, network_thread_);
        if (!(flags1 & RTCP_MUX)) {
          fake_rtcp_dtls_transport1_ = std::make_unique<FakeDtlsTransport>(
              env_, "channel1", ICE_CANDIDATE_COMPONENT_RTCP, network_thread_);
        }
        if (flags1 & DTLS) {
          auto cert1 = RTCCertificate::Create(
              SSLIdentity::Create("session1", KT_DEFAULT));
          fake_rtp_dtls_transport1_->SetLocalCertificate(cert1);
          if (fake_rtcp_dtls_transport1_) {
            fake_rtcp_dtls_transport1_->SetLocalCertificate(cert1);
          }
        }
      }
      // Based on flags, create fake DTLS or raw packet transports.
      if (flags2 & RAW_PACKET_TRANSPORT) {
        fake_rtp_packet_transport2_ =
            std::make_unique<FakePacketTransport>(env_, "channel2_rtp");
        if (!(flags2 & RTCP_MUX)) {
          fake_rtcp_packet_transport2_ =
              std::make_unique<FakePacketTransport>(env_, "channel2_rtcp");
        }
      } else {
        // Confirmed to work with KT_RSA and KT_ECDSA.
        fake_rtp_dtls_transport2_ = std::make_unique<FakeDtlsTransport>(
            env_, "channel2", ICE_CANDIDATE_COMPONENT_RTP, network_thread_);
        if (!(flags2 & RTCP_MUX)) {
          fake_rtcp_dtls_transport2_ = std::make_unique<FakeDtlsTransport>(
              env_, "channel2", ICE_CANDIDATE_COMPONENT_RTCP, network_thread_);
        }
        if (flags2 & DTLS) {
          auto cert2 = RTCCertificate::Create(
              SSLIdentity::Create("session2", KT_DEFAULT));
          fake_rtp_dtls_transport2_->SetLocalCertificate(cert2);
          if (fake_rtcp_dtls_transport2_) {
            fake_rtcp_dtls_transport2_->SetLocalCertificate(cert2);
          }
        }
      }
      rtp_transport1_ = CreateRtpTransportBasedOnFlags(
          fake_rtp_packet_transport1_.get(), fake_rtcp_packet_transport1_.get(),
          fake_rtp_dtls_transport1_.get(), fake_rtcp_dtls_transport1_.get(),
          flags1);
      rtp_transport2_ = CreateRtpTransportBasedOnFlags(
          fake_rtp_packet_transport2_.get(), fake_rtcp_packet_transport2_.get(),
          fake_rtp_dtls_transport2_.get(), fake_rtcp_dtls_transport2_.get(),
          flags2);
    });

    channel1_ = CreateChannel(worker_thread, network_thread_, std::move(ch1s),
                              std::move(ch1r), rtp_transport1_.get(), flags1);
    channel2_ = CreateChannel(worker_thread, network_thread_, std::move(ch2s),
                              std::move(ch2r), rtp_transport2_.get(), flags2);
    CreateContent(flags1, kPcmuCodec, kH264Codec, &local_media_content1_);
    CreateContent(flags2, kPcmuCodec, kH264Codec, &local_media_content2_);
    CopyContent(local_media_content1_, &remote_media_content1_);
    CopyContent(local_media_content2_, &remote_media_content2_);

    // Add stream information (SSRC) to the local content but not to the remote
    // content. This means that we per default know the SSRC of what we send but
    // not what we receive.
    AddLegacyStreamInContent(kSsrc1, flags1, &local_media_content1_);
    AddLegacyStreamInContent(kSsrc2, flags2, &local_media_content2_);

    // If SSRC_MUX is used we also need to know the SSRC of the incoming stream.
    if (flags1 & SSRC_MUX) {
      AddLegacyStreamInContent(kSsrc1, flags1, &remote_media_content1_);
    }
    if (flags2 & SSRC_MUX) {
      AddLegacyStreamInContent(kSsrc2, flags2, &remote_media_content2_);
    }
  }

  std::unique_ptr<typename T::Channel> CreateChannel(
      Thread* worker_thread,
      Thread* network_thread,
      std::unique_ptr<typename T::MediaSendChannel> ch_send,
      std::unique_ptr<typename T::MediaReceiveChannel> ch_receive,
      RtpTransportInternal* rtp_transport,
      int flags);

  std::unique_ptr<RtpTransportInternal> CreateRtpTransportBasedOnFlags(
      PacketTransportInternal* rtp_packet_transport,
      PacketTransportInternal* rtcp_packet_transport,
      DtlsTransportInternal* rtp_dtls_transport,
      DtlsTransportInternal* rtcp_dtls_transport,
      int flags) {
    if (flags & RTCP_MUX) {
      rtcp_packet_transport = nullptr;
      rtcp_dtls_transport = nullptr;
    }

    if (flags & DTLS) {
      return CreateDtlsSrtpTransport(rtp_dtls_transport, rtcp_dtls_transport);
    } else {
      if (flags & RAW_PACKET_TRANSPORT) {
        return CreateUnencryptedTransport(rtp_packet_transport,
                                          rtcp_packet_transport);
      } else {
        return CreateUnencryptedTransport(rtp_dtls_transport,
                                          rtcp_dtls_transport);
      }
    }
  }

  // Unininitializes the channels on the network thread.
  void DeinitChannels() {
    if (!channel1_ && !channel2_)
      return;
    SendTask(network_thread_, [this]() {
      if (channel1_) {
        RTC_DCHECK_RUN_ON(channel1_->network_thread());
        channel1_->SetRtpTransport(nullptr);
      }
      if (channel2_) {
        RTC_DCHECK_RUN_ON(channel2_->network_thread());
        channel2_->SetRtpTransport(nullptr);
      }
    });
  }

  std::unique_ptr<RtpTransport> CreateUnencryptedTransport(
      PacketTransportInternal* rtp_packet_transport,
      PacketTransportInternal* rtcp_packet_transport) {
    auto rtp_transport = std::make_unique<RtpTransport>(
        rtcp_packet_transport == nullptr, env_.field_trials());

    SendTask(network_thread_,
             [&rtp_transport, rtp_packet_transport, rtcp_packet_transport] {
               rtp_transport->SetRtpPacketTransport(rtp_packet_transport);
               if (rtcp_packet_transport) {
                 rtp_transport->SetRtcpPacketTransport(rtcp_packet_transport);
               }
             });
    return rtp_transport;
  }

  std::unique_ptr<DtlsSrtpTransport> CreateDtlsSrtpTransport(
      DtlsTransportInternal* rtp_dtls_transport,
      DtlsTransportInternal* rtcp_dtls_transport) {
    auto dtls_srtp_transport = std::make_unique<DtlsSrtpTransport>(
        rtcp_dtls_transport == nullptr, env_.field_trials());

    SendTask(network_thread_,
             [&dtls_srtp_transport, rtp_dtls_transport, rtcp_dtls_transport] {
               dtls_srtp_transport->SetDtlsTransports(rtp_dtls_transport,
                                                      rtcp_dtls_transport);
             });
    return dtls_srtp_transport;
  }

  void ConnectFakeTransports() {
    SendTask(network_thread_, [this] {
      bool asymmetric = false;
      // Depending on test flags, could be using DTLS or raw packet transport.
      if (fake_rtp_dtls_transport1_ && fake_rtp_dtls_transport2_) {
        fake_rtp_dtls_transport1_->SetDestination(
            fake_rtp_dtls_transport2_.get(), asymmetric);
      }
      if (fake_rtcp_dtls_transport1_ && fake_rtcp_dtls_transport2_) {
        fake_rtcp_dtls_transport1_->SetDestination(
            fake_rtcp_dtls_transport2_.get(), asymmetric);
      }
      if (fake_rtp_packet_transport1_ && fake_rtp_packet_transport2_) {
        fake_rtp_packet_transport1_->SetDestination(
            fake_rtp_packet_transport2_.get(), asymmetric);
      }
      if (fake_rtcp_packet_transport1_ && fake_rtcp_packet_transport2_) {
        fake_rtcp_packet_transport1_->SetDestination(
            fake_rtcp_packet_transport2_.get(), asymmetric);
      }
    });
    // The transport becoming writable will asynchronously update the send state
    // on the worker thread; since this test uses the main thread as the worker
    // thread, we must process the message queue for this to occur.
    WaitForThreads();
  }

  bool SendInitiate() {
    RTCError result =
        channel1_->SetLocalContent(&local_media_content1_, SdpType::kOffer);
    if (result.ok()) {
      channel1_->Enable(true);
      FlushCurrentThread();
      result =
          channel2_->SetRemoteContent(&remote_media_content1_, SdpType::kOffer);
      if (result.ok()) {
        ConnectFakeTransports();
        result = channel2_->SetLocalContent(&local_media_content2_,
                                            SdpType::kAnswer);
      }
    }
    return result.ok();
  }

  bool SendAccept() {
    channel2_->Enable(true);
    FlushCurrentThread();
    return channel1_
        ->SetRemoteContent(&remote_media_content2_, SdpType::kAnswer)
        .ok();
  }

  bool SendOffer() {
    RTCError result =
        channel1_->SetLocalContent(&local_media_content1_, SdpType::kOffer);
    if (result.ok()) {
      channel1_->Enable(true);
      result =
          channel2_->SetRemoteContent(&remote_media_content1_, SdpType::kOffer);
    }
    return result.ok();
  }

  bool SendProvisionalAnswer() {
    RTCError result =
        channel2_->SetLocalContent(&local_media_content2_, SdpType::kPrAnswer);
    if (result.ok()) {
      channel2_->Enable(true);
      result = channel1_->SetRemoteContent(&remote_media_content2_,
                                           SdpType::kPrAnswer);
      ConnectFakeTransports();
    }
    return result.ok();
  }

  bool SendFinalAnswer() {
    RTCError result =
        channel2_->SetLocalContent(&local_media_content2_, SdpType::kAnswer);
    if (result.ok()) {
      result = channel1_->SetRemoteContent(&remote_media_content2_,
                                           SdpType::kAnswer);
    }
    return result.ok();
  }

  void SendRtp(typename T::MediaSendChannel* media_channel, Buffer data) {
    network_thread_->PostTask(SafeTask(
        network_thread_safety_, [media_channel, data = std::move(data)]() {
          media_channel->SendPacket(data, AsyncSocketPacketOptions());
        }));
  }

  void SendRtp1() { SendRtp1(Buffer(rtp_packet_.data(), rtp_packet_.size())); }

  void SendRtp1(Buffer data) {
    SendRtp(media_send_channel1_impl(), std::move(data));
  }

  void SendRtp2() { SendRtp2(Buffer(rtp_packet_.data(), rtp_packet_.size())); }

  void SendRtp2(Buffer data) {
    SendRtp(media_send_channel2_impl(), std::move(data));
  }

  // Methods to send custom data.
  void SendCustomRtp1(uint32_t ssrc, int sequence_number, int pl_type = -1) {
    SendRtp1(CreateRtpData(ssrc, sequence_number, pl_type));
  }
  void SendCustomRtp2(uint32_t ssrc, int sequence_number, int pl_type = -1) {
    SendRtp2(CreateRtpData(ssrc, sequence_number, pl_type));
  }

  bool CheckRtp1() {
    return media_receive_channel1_impl()->CheckRtp(rtp_packet_.data(),
                                                   rtp_packet_.size());
  }
  bool CheckRtp2() {
    return media_receive_channel2_impl()->CheckRtp(rtp_packet_.data(),
                                                   rtp_packet_.size());
  }
  // Methods to check custom data.
  bool CheckCustomRtp1(uint32_t ssrc, int sequence_number, int pl_type = -1) {
    Buffer data = CreateRtpData(ssrc, sequence_number, pl_type);
    return media_receive_channel1_impl()->CheckRtp(data.data(), data.size());
  }
  bool CheckCustomRtp2(uint32_t ssrc, int sequence_number, int pl_type = -1) {
    Buffer data = CreateRtpData(ssrc, sequence_number, pl_type);
    return media_receive_channel2_impl()->CheckRtp(data.data(), data.size());
  }
  Buffer CreateRtpData(uint32_t ssrc, int sequence_number, int pl_type) {
    Buffer data(rtp_packet_.data(), rtp_packet_.size());
    // Set SSRC in the rtp packet copy.
    SetBE32(std::span<uint8_t>(data).subspan(8, 4), ssrc);
    SetBE16(std::span<uint8_t>(data).subspan(2, 2), sequence_number);
    if (pl_type >= 0) {
      Set8(data, 1, static_cast<uint8_t>(pl_type));
    }
    return data;
  }

  void CreateContent(int flags,
                     const Codec& audio_codec,
                     const Codec& video_codec,
                     typename T::Content* content) {
    // overridden in specialized classes
  }
  void CopyContent(const typename T::Content& source,
                   typename T::Content* content) {
    // overridden in specialized classes
  }

  // Creates a MediaContent with one stream.
  // kPcmuCodec is used as audio codec and kH264Codec is used as video codec.
  typename T::Content* CreateMediaContentWithStream(uint32_t ssrc) {
    typename T::Content* content = new typename T::Content();
    CreateContent(0, kPcmuCodec, kH264Codec, content);
    AddLegacyStreamInContent(ssrc, 0, content);
    return content;
  }

  // Will manage the lifetime of a CallThread, making sure it's
  // destroyed before this object goes out of scope.
  class ScopedCallThread {
   public:
    explicit ScopedCallThread(absl::AnyInvocable<void() &&> functor)
        : thread_(Thread::Create()) {
      thread_->Start();
      thread_->PostTask(std::move(functor));
    }

    ~ScopedCallThread() { thread_->Stop(); }

    Thread* thread() { return thread_.get(); }

   private:
    std::unique_ptr<Thread> thread_;
  };

  CandidatePairInterface* last_selected_candidate_pair() {
    return last_selected_candidate_pair_;
  }

  void AddLegacyStreamInContent(uint32_t ssrc,
                                int flags,
                                typename T::Content* content) {
    // Base implementation.
  }

  // Utility method that calls BaseChannel::srtp_active() on the network thread
  // and returns the result. The `srtp_active()` state is maintained on the
  // network thread, which callers need to factor in.
  bool IsSrtpActive(std::unique_ptr<typename T::Channel>& channel) {
    RTC_DCHECK(channel.get());
    bool result;
    SendTask(network_thread_, [&] { result = channel->srtp_active(); });
    return result;
  }

  // Returns true iff the transport is set for a channel and rtcp_mux_enabled()
  // returns true.
  bool IsRtcpMuxEnabled(std::unique_ptr<typename T::Channel>& channel) {
    RTC_DCHECK(channel.get());
    bool result;
    SendTask(network_thread_, [&] {
      result = channel->rtp_transport() &&
               channel->rtp_transport()->rtcp_mux_enabled();
    });
    return result;
  }

  // Tests that can be used by derived classes.

  // Basic sanity check.
  void TestInit() {
    CreateChannels(0, 0);
    EXPECT_FALSE(IsSrtpActive(channel1_));
    EXPECT_FALSE(media_send_channel1_impl()->sending());
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel1_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel1_impl()->send_codecs().empty());
    EXPECT_TRUE(media_receive_channel1_impl()->recv_streams().empty());
    // Basic sanity test for send and receive channel objects
    EXPECT_EQ(channel1_->media_send_channel()->media_type(),
              media_send_channel1_impl()->media_type());
    EXPECT_EQ(channel1_->media_receive_channel()->media_type(),
              media_receive_channel1_impl()->media_type());
    EXPECT_EQ(channel1_->media_send_channel()->media_type(),
              channel1_->media_receive_channel()->media_type());
  }

  // Test that SetLocalContent and SetRemoteContent properly configure
  // the codecs.
  void TestSetContents() {
    CreateChannels(0, 0);
    typename T::Content content;
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    EXPECT_TRUE(channel1_->SetLocalContent(&content, SdpType::kOffer).ok());
    EXPECT_EQ(0U, media_send_channel1_impl()->send_codecs().size());
    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kAnswer).ok());
    ASSERT_EQ(1U, media_send_channel1_impl()->send_codecs().size());
    EXPECT_EQ(content.codecs()[0],
              media_send_channel1_impl()->send_codecs()[0]);
  }

  void TestRemovesExtensionNotPresentInRemoteAnswer() {
    typename T::Content local;
    typename T::Content remote;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local);
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &remote);
    local.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     RtpHeaderExtensionId(1)),
        RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(2)),
    });
    remote.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(2)),
    });

    CreateChannels(0, 0);
    ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kOffer).ok());
    ASSERT_TRUE(channel1_->SetRemoteContent(&remote, SdpType::kAnswer).ok());

    EXPECT_THAT(media_receive_channel1_impl()->recv_extensions(),
                ElementsAre(AllOf(
                    Field("id", &RtpExtension::id, RtpHeaderExtensionId(2)),
                    Field("uri", &RtpExtension::uri,
                          RtpExtension::kVideoRotationUri))));
    EXPECT_THAT(media_send_channel1_impl()->send_extensions(),
                ElementsAre(AllOf(
                    Field("id", &RtpExtension::id, RtpHeaderExtensionId(2)),
                    Field("uri", &RtpExtension::uri,
                          RtpExtension::kVideoRotationUri))));
  }
  void TestRemovesExtensionNotPresentInLocalAnswer() {
    typename T::Content local;
    typename T::Content remote;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local);
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &remote);
    local.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(1)),
    });
    remote.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     RtpHeaderExtensionId(2)),
        RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(1)),
    });

    CreateChannels(0, 0);
    ASSERT_TRUE(channel1_->SetRemoteContent(&remote, SdpType::kOffer).ok());
    ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kAnswer).ok());

    EXPECT_THAT(media_receive_channel1_impl()->recv_extensions(),
                ElementsAre(AllOf(
                    Field("id", &RtpExtension::id, RtpHeaderExtensionId(1)),
                    Field("uri", &RtpExtension::uri,
                          RtpExtension::kVideoRotationUri))));
    EXPECT_THAT(media_send_channel1_impl()->send_extensions(),
                ElementsAre(AllOf(
                    Field("id", &RtpExtension::id, RtpHeaderExtensionId(1)),
                    Field("uri", &RtpExtension::uri,
                          RtpExtension::kVideoRotationUri))));
  }

  void TestDuplicateRtpHeaderExtensionIds() {
    typename T::Content local;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local);
    local.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     RtpHeaderExtensionId(1)),
        RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(1)),
    });

    CreateChannels(0, 0);
    RTCError error = channel1_->SetLocalContent(&local, SdpType::kOffer);
    EXPECT_FALSE(error.ok());
    EXPECT_THAT(error.message(), HasSubstr("Duplicate extension ID"));
  }

  void TestRtpHeaderExtensionIdOutOfRangeHigh() {
    typename T::Content local;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local);
    local.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     RtpHeaderExtensionId(256)),
    });

    CreateChannels(0, 0);
    RTCError error = channel1_->SetLocalContent(&local, SdpType::kOffer);
    EXPECT_FALSE(error.ok());
    EXPECT_THAT(error.message(), HasSubstr("Bad extension ID"));
  }

  void TestRtpHeaderExtensionIdOutOfRangeLow() {
    typename T::Content local;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local);
    local.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     RtpHeaderExtensionId(0)),
    });

    CreateChannels(0, 0);
    RTCError error = channel1_->SetLocalContent(&local, SdpType::kOffer);
    EXPECT_FALSE(error.ok());
    EXPECT_THAT(error.message(), HasSubstr("Bad extension ID"));
  }

  void TestRtpHeaderExtensionIdReassignment() {
    typename T::Content local;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local);
    local.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     RtpHeaderExtensionId(1)),
    });

    CreateChannels(0, 0);
    ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kOffer).ok());

    typename T::Content local_updated;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local_updated);
    local_updated.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(1)),
    });
    RTCError error =
        channel1_->SetLocalContent(&local_updated, SdpType::kOffer);
    EXPECT_FALSE(error.ok());
    EXPECT_THAT(error.message(), HasSubstr("RTP extension ID reassignment"));
  }

  void TestRtpHeaderExtensionIdHistoryReassignment() {
    typename T::Content local;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local);
    local.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     RtpHeaderExtensionId(1)),
    });

    CreateChannels(0, 0);
    ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kOffer).ok());

    typename T::Content local_empty;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local_empty);
    local_empty.set_rtp_header_extensions({});
    ASSERT_TRUE(channel1_->SetLocalContent(&local_empty, SdpType::kOffer).ok());

    typename T::Content local_updated;
    CreateContent(/*flags=*/0, kPcmuCodec, kH264Codec, &local_updated);
    local_updated.set_rtp_header_extensions({
        RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(1)),
    });
    RTCError error =
        channel1_->SetLocalContent(&local_updated, SdpType::kOffer);
    // Expected to succeed because the mapping for ID 1 was cleared by the
    // previous SetLocalContent call (which set extensions to empty).
    // RtpTransport allows reuse when not in use by any active MID.
    EXPECT_TRUE(error.ok());
  }

  // Test that SetLocalContent and SetRemoteContent properly configure
  // extmap-allow-mixed.
  void TestSetContentsExtmapAllowMixedCaller(bool offer, bool answer) {
    // For a caller, SetLocalContent() is called first with an offer and next
    // SetRemoteContent() is called with the answer.
    CreateChannels(0, 0);
    typename T::Content content;
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    auto offer_enum = offer ? (T::Content::AttributeLevel::kSession)
                            : (T::Content::AttributeLevel::kNone);
    auto answer_enum = answer ? (T::Content::AttributeLevel::kSession)
                              : (T::Content::AttributeLevel::kNone);
    content.set_extmap_allow_mixed_level(offer_enum);
    EXPECT_TRUE(channel1_->SetLocalContent(&content, SdpType::kOffer).ok());
    content.set_extmap_allow_mixed_level(answer_enum);
    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kAnswer).ok());
    EXPECT_EQ(answer, media_send_channel1_impl()->ExtmapAllowMixed());
  }

  void TestSetContentsExtmapAllowMixedCallee(bool offer, bool answer) {
    // For a callee, SetRemoteContent() is called first with an offer and next
    // SetLocalContent() is called with the answer.
    CreateChannels(0, 0);
    typename T::Content content;
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    auto offer_enum = offer ? (T::Content::AttributeLevel::kSession)
                            : (T::Content::AttributeLevel::kNone);
    auto answer_enum = answer ? (T::Content::AttributeLevel::kSession)
                              : (T::Content::AttributeLevel::kNone);
    content.set_extmap_allow_mixed_level(offer_enum);
    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kOffer).ok());
    content.set_extmap_allow_mixed_level(answer_enum);
    EXPECT_TRUE(channel1_->SetLocalContent(&content, SdpType::kAnswer).ok());
    EXPECT_EQ(answer, media_send_channel1_impl()->ExtmapAllowMixed());
  }

  // Test that SetLocalContent and SetRemoteContent properly deals
  // with an empty offer.
  void TestSetContentsNullOffer() {
    CreateChannels(0, 0);
    typename T::Content content;
    EXPECT_TRUE(channel1_->SetLocalContent(&content, SdpType::kOffer).ok());
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    EXPECT_EQ(0U, media_send_channel1_impl()->send_codecs().size());
    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kAnswer).ok());
    ASSERT_EQ(1U, media_send_channel1_impl()->send_codecs().size());
    EXPECT_EQ(content.codecs()[0],
              media_send_channel1_impl()->send_codecs()[0]);
  }

  // Test that SetLocalContent and SetRemoteContent properly set RTCP
  // mux.
  void TestSetContentsRtcpMux() {
    CreateChannels(0, 0);
    typename T::Content content;
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    // Both sides agree on mux. Should no longer be a separate RTCP channel.
    content.set_rtcp_mux(true);
    EXPECT_TRUE(channel1_->SetLocalContent(&content, SdpType::kOffer).ok());
    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kAnswer).ok());
    // Only initiator supports mux. Should still have a separate RTCP channel.
    EXPECT_TRUE(channel2_->SetLocalContent(&content, SdpType::kOffer).ok());
    content.set_rtcp_mux(false);
    EXPECT_TRUE(channel2_->SetRemoteContent(&content, SdpType::kAnswer).ok());
  }

  // Test that SetLocalContent and SetRemoteContent properly set RTCP
  // reduced_size.
  void TestSetContentsRtcpReducedSize() {
    CreateChannels(0, 0);
    typename T::Content content;
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    // Both sides agree on reduced size.
    content.set_rtcp_reduced_size(true);
    // The RTCP mode is a send property and should be configured based on
    // the remote content and not the local content.
    EXPECT_TRUE(channel1_->SetLocalContent(&content, SdpType::kOffer).ok());
    EXPECT_EQ(media_receive_channel1_impl()->RtcpMode(), RtcpMode::kCompound);
    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kAnswer).ok());
    EXPECT_EQ(media_receive_channel1_impl()->RtcpMode(),
              RtcpMode::kReducedSize);
    // Only initiator supports reduced size.
    EXPECT_TRUE(channel2_->SetLocalContent(&content, SdpType::kOffer).ok());
    EXPECT_EQ(media_receive_channel2_impl()->RtcpMode(), RtcpMode::kCompound);
    content.set_rtcp_reduced_size(false);
    EXPECT_TRUE(channel2_->SetRemoteContent(&content, SdpType::kAnswer).ok());
    EXPECT_EQ(media_receive_channel2_impl()->RtcpMode(), RtcpMode::kCompound);
    // Peer renegotiates without reduced size.
    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kAnswer).ok());
    EXPECT_EQ(media_receive_channel1_impl()->RtcpMode(), RtcpMode::kCompound);
  }

  // Test that SetLocalContent and SetRemoteContent properly
  // handles adding and removing StreamParams when the action is a full
  // SdpType::kOffer / SdpType::kAnswer.
  void TestChangeStreamParamsInContent() {
    StreamParams stream1;
    stream1.id = "stream1";
    stream1.ssrcs.push_back(kSsrc1);
    stream1.cname = "stream1_cname";

    StreamParams stream2;
    stream2.id = "stream2";
    stream2.ssrcs.push_back(kSsrc2);
    stream2.cname = "stream2_cname";

    // Setup a call where channel 1 send `stream1` to channel 2.
    CreateChannels(0, 0);
    typename T::Content content1;
    CreateContent(0, kPcmuCodec, kH264Codec, &content1);
    content1.AddStream(stream1);
    EXPECT_TRUE(channel1_->SetLocalContent(&content1, SdpType::kOffer).ok());
    channel1_->Enable(true);
    EXPECT_EQ(1u, media_send_channel1_impl()->send_streams().size());
    EXPECT_TRUE(channel2_->SetRemoteContent(&content1, SdpType::kOffer).ok());
    EXPECT_EQ(1u, media_receive_channel2_impl()->recv_streams().size());
    ConnectFakeTransports();

    // Channel 2 do not send anything.
    typename T::Content content2;
    CreateContent(0, kPcmuCodec, kH264Codec, &content2);
    EXPECT_TRUE(channel1_->SetRemoteContent(&content2, SdpType::kAnswer).ok());
    EXPECT_EQ(0u, media_receive_channel1_impl()->recv_streams().size());
    EXPECT_TRUE(channel2_->SetLocalContent(&content2, SdpType::kAnswer).ok());
    channel2_->Enable(true);
    EXPECT_EQ(0u, media_send_channel2_impl()->send_streams().size());

    SendCustomRtp1(kSsrc1, 0);
    WaitForThreads();
    EXPECT_TRUE(CheckCustomRtp2(kSsrc1, 0));

    // Let channel 2 update the content by sending `stream2` and enable SRTP.
    typename T::Content content3;
    CreateContent(0, kPcmuCodec, kH264Codec, &content3);
    content3.AddStream(stream2);
    EXPECT_TRUE(channel2_->SetLocalContent(&content3, SdpType::kOffer).ok());
    ASSERT_EQ(1u, media_send_channel2_impl()->send_streams().size());
    EXPECT_EQ(stream2, media_send_channel2_impl()->send_streams()[0]);

    EXPECT_TRUE(channel1_->SetRemoteContent(&content3, SdpType::kOffer).ok());
    ASSERT_EQ(1u, media_receive_channel1_impl()->recv_streams().size());
    EXPECT_EQ(stream2, media_receive_channel1_impl()->recv_streams()[0]);

    // Channel 1 replies but stop sending stream1.
    typename T::Content content4;
    CreateContent(0, kPcmuCodec, kH264Codec, &content4);
    EXPECT_TRUE(channel1_->SetLocalContent(&content4, SdpType::kAnswer).ok());
    EXPECT_EQ(0u, media_send_channel1_impl()->send_streams().size());

    EXPECT_TRUE(channel2_->SetRemoteContent(&content4, SdpType::kAnswer).ok());
    EXPECT_EQ(0u, media_receive_channel2_impl()->recv_streams().size());

    SendCustomRtp2(kSsrc2, 0);
    WaitForThreads();
    EXPECT_TRUE(CheckCustomRtp1(kSsrc2, 0));
  }

  class MockReceiveChannel : public T::MediaReceiveChannel {
   public:
    MockReceiveChannel(const typename T::Options& options,
                       Thread* network_thread)
        : T::MediaReceiveChannel(options, network_thread) {}

    void OnDemuxerCriteriaUpdatePending() override {
      ++pending_count_;
      T::MediaReceiveChannel::OnDemuxerCriteriaUpdatePending();
    }

    void OnDemuxerCriteriaUpdateComplete() override {
      --pending_count_;
      T::MediaReceiveChannel::OnDemuxerCriteriaUpdateComplete();
    }

    bool AddRecvStream(const StreamParams& sp) override {
      add_stream_called_ = true;
      if (pending_count_ <= 0) {
        criteria_not_pending_during_add_stream_ = true;
      }
      return T::MediaReceiveChannel::AddRecvStream(sp);
    }

    bool add_stream_called() const { return add_stream_called_; }
    bool criteria_not_pending_during_add_stream() const {
      return criteria_not_pending_during_add_stream_;
    }

   private:
    int pending_count_ = 0;
    bool add_stream_called_ = false;
    bool criteria_not_pending_during_add_stream_ = false;
  };

  void TestUpdateRemoteStreamsRaceWithRtpPacket() {
    auto ch1r = std::make_unique<MockReceiveChannel>(typename T::Options(),
                                                     network_thread_);
    MockReceiveChannel* mock_ch1r = ch1r.get();

    CreateChannels(std::make_unique<typename T::MediaSendChannel>(
                       typename T::Options(), network_thread_),
                   std::move(ch1r),
                   std::make_unique<typename T::MediaSendChannel>(
                       typename T::Options(), network_thread_),
                   std::make_unique<typename T::MediaReceiveChannel>(
                       typename T::Options(), network_thread_),
                   0, 0);

    // Configure a new stream `stream1` to be added
    StreamParams stream1;
    stream1.id = "stream1";
    stream1.ssrcs.push_back(kSsrc1);
    stream1.cname = "stream1_cname";

    typename T::Content content1;
    CreateContent(0, kPcmuCodec, kH264Codec, &content1);
    content1.AddStream(stream1);

    ASSERT_TRUE(channel1_->SetLocalContent(&content1, SdpType::kOffer).ok());
    channel1_->Enable(true);

    typename T::Content content2;
    CreateContent(0, kPcmuCodec, kH264Codec, &content2);
    ASSERT_TRUE(channel2_->SetRemoteContent(&content1, SdpType::kOffer).ok());
    ConnectFakeTransports();

    // Configure answer adding stream2.
    StreamParams stream2;
    stream2.id = "stream2";
    stream2.ssrcs.push_back(kSsrc2);
    stream2.cname = "stream2_cname";

    typename T::Content content3;
    CreateContent(0, kPcmuCodec, kH264Codec, &content3);
    content3.AddStream(stream2);

    // Call SetRemoteContent.
    RTCError error = channel1_->SetRemoteContent(&content3, SdpType::kAnswer);

    EXPECT_TRUE(error.ok()) << "SetRemoteContent failed: " << error.message();
    EXPECT_TRUE(mock_ch1r->add_stream_called());
    EXPECT_FALSE(mock_ch1r->criteria_not_pending_during_add_stream())
        << "Race condition detected: demuxer criteria was not pending during "
           "AddRecvStream!";
  }

  // Test that we only start playout and sending at the right times.
  void TestPlayoutAndSendingStates() {
    CreateChannels(0, 0);
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel1_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel1_impl()->sending());
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel2_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel2_impl()->sending());
    channel1_->Enable(true);
    FlushCurrentThread();
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel1_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel1_impl()->sending());
    EXPECT_TRUE(
        channel1_->SetLocalContent(&local_media_content1_, SdpType::kOffer)
            .ok());
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel1_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel1_impl()->sending());
    EXPECT_TRUE(
        channel2_->SetRemoteContent(&local_media_content1_, SdpType::kOffer)
            .ok());
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel2_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel2_impl()->sending());
    EXPECT_TRUE(
        channel2_->SetLocalContent(&local_media_content2_, SdpType::kAnswer)
            .ok());
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel2_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel2_impl()->sending());
    ConnectFakeTransports();
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel1_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel1_impl()->sending());
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel2_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel2_impl()->sending());
    channel2_->Enable(true);
    FlushCurrentThread();
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel2_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel2_impl()->sending());
    EXPECT_TRUE(
        channel1_->SetRemoteContent(&local_media_content2_, SdpType::kAnswer)
            .ok());
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel1_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel1_impl()->sending());
  }

  // Test that changing the MediaContentDirection in the local and remote
  // session description start playout and sending at the right time.
  void TestMediaContentDirection() {
    CreateChannels(0, 0);
    typename T::Content content1;
    CreateContent(0, kPcmuCodec, kH264Codec, &content1);
    typename T::Content content2;
    CreateContent(0, kPcmuCodec, kH264Codec, &content2);
    // Set `content2` to be InActive.
    content2.set_direction(RtpTransceiverDirection::kInactive);

    channel1_->Enable(true);
    channel2_->Enable(true);
    FlushCurrentThread();
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel1_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel1_impl()->sending());
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel2_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel2_impl()->sending());

    EXPECT_TRUE(channel1_->SetLocalContent(&content1, SdpType::kOffer).ok());
    EXPECT_TRUE(channel2_->SetRemoteContent(&content1, SdpType::kOffer).ok());
    EXPECT_TRUE(channel2_->SetLocalContent(&content2, SdpType::kPrAnswer).ok());
    EXPECT_TRUE(
        channel1_->SetRemoteContent(&content2, SdpType::kPrAnswer).ok());
    ConnectFakeTransports();

    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel1_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel1_impl()->sending());  // remote InActive
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel2_impl()->playout());  // local InActive
    }
    EXPECT_FALSE(media_send_channel2_impl()->sending());  // local InActive

    // Update `content2` to be RecvOnly.
    content2.set_direction(RtpTransceiverDirection::kRecvOnly);
    EXPECT_TRUE(channel2_->SetLocalContent(&content2, SdpType::kPrAnswer).ok());
    EXPECT_TRUE(
        channel1_->SetRemoteContent(&content2, SdpType::kPrAnswer).ok());

    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel1_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel1_impl()->sending());
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel2_impl()->playout());  // local RecvOnly
    }
    EXPECT_FALSE(media_send_channel2_impl()->sending());  // local RecvOnly

    // Update `content2` to be SendRecv.
    content2.set_direction(RtpTransceiverDirection::kSendRecv);
    EXPECT_TRUE(channel2_->SetLocalContent(&content2, SdpType::kAnswer).ok());
    EXPECT_TRUE(channel1_->SetRemoteContent(&content2, SdpType::kAnswer).ok());

    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel1_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel1_impl()->sending());
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel2_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel2_impl()->sending());

    // Update `content2` to be inactive on the receiver while sending at the
    // sender.
    content2.set_direction(RtpTransceiverDirection::kInactive);
    EXPECT_TRUE(channel1_->SetLocalContent(&content1, SdpType::kOffer).ok());
    EXPECT_TRUE(channel2_->SetRemoteContent(&content1, SdpType::kOffer).ok());
    EXPECT_TRUE(channel2_->SetLocalContent(&content2, SdpType::kAnswer).ok());
    content2.set_direction(RtpTransceiverDirection::kRecvOnly);
    EXPECT_TRUE(channel1_->SetRemoteContent(&content2, SdpType::kAnswer).ok());
    if (verify_playout_) {
      EXPECT_FALSE(media_receive_channel2_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel1_impl()->sending());

    // Re-enable `content2`.
    content2.set_direction(RtpTransceiverDirection::kSendRecv);
    EXPECT_TRUE(channel1_->SetLocalContent(&content1, SdpType::kOffer).ok());
    EXPECT_TRUE(channel2_->SetRemoteContent(&content1, SdpType::kOffer).ok());
    EXPECT_TRUE(channel2_->SetLocalContent(&content2, SdpType::kAnswer).ok());
    EXPECT_TRUE(channel1_->SetRemoteContent(&content2, SdpType::kAnswer).ok());
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel2_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel1_impl()->sending());
  }

  // Test setting up a call.
  void TestCallSetup() {
    CreateChannels(0, 0);
    EXPECT_FALSE(IsSrtpActive(channel1_));
    EXPECT_TRUE(SendInitiate());
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel1_impl()->playout());
    }
    EXPECT_FALSE(media_send_channel1_impl()->sending());
    EXPECT_TRUE(SendAccept());
    EXPECT_FALSE(IsSrtpActive(channel1_));
    EXPECT_TRUE(media_send_channel1_impl()->sending());
    EXPECT_EQ(1U, media_send_channel1_impl()->send_codecs().size());
    if (verify_playout_) {
      EXPECT_TRUE(media_receive_channel2_impl()->playout());
    }
    EXPECT_TRUE(media_send_channel2_impl()->sending());
    EXPECT_EQ(1U, media_send_channel2_impl()->send_codecs().size());
  }

  // Send voice RTP data to the other side and ensure it gets there.
  void SendRtpToRtp() {
    CreateChannels(RTCP_MUX, RTCP_MUX);
    EXPECT_TRUE(SendInitiate());
    EXPECT_TRUE(SendAccept());
    EXPECT_TRUE(IsRtcpMuxEnabled(channel1_));
    EXPECT_TRUE(IsRtcpMuxEnabled(channel2_));
    SendRtp1();
    SendRtp2();
    WaitForThreads();
    EXPECT_TRUE(CheckRtp1());
    EXPECT_TRUE(CheckRtp2());
  }

  void TestDeinit() {
    CreateChannels(0, 0);
    EXPECT_TRUE(SendInitiate());
    EXPECT_TRUE(SendAccept());
    SendRtp1();
    SendRtp2();

    DeinitChannels();

    // Do not wait, destroy channels.
    channel1_.reset(nullptr);
    channel2_.reset(nullptr);
  }

  void SendDtlsSrtpToDtlsSrtp(int flags1, int flags2) {
    CreateChannels(flags1 | DTLS, flags2 | DTLS);
    EXPECT_FALSE(IsSrtpActive(channel1_));
    EXPECT_FALSE(IsSrtpActive(channel2_));
    EXPECT_TRUE(SendInitiate());
    WaitForThreads();
    EXPECT_TRUE(SendAccept());
    EXPECT_TRUE(IsSrtpActive(channel1_));
    EXPECT_TRUE(IsSrtpActive(channel2_));
    SendRtp1();
    SendRtp2();
    WaitForThreads();
    EXPECT_TRUE(CheckRtp1());
    EXPECT_TRUE(CheckRtp2());
  }

  // Test that we can send and receive early media when a provisional answer is
  // sent and received. The test uses SRTP, RTCP mux and SSRC mux.
  void SendEarlyMediaUsingRtcpMuxSrtp() {
    int sequence_number1_1 = 0, sequence_number2_2 = 0;

    CreateChannels(SSRC_MUX | RTCP_MUX | DTLS, SSRC_MUX | RTCP_MUX | DTLS);
    EXPECT_TRUE(SendOffer());
    EXPECT_TRUE(SendProvisionalAnswer());
    EXPECT_TRUE(IsSrtpActive(channel1_));
    EXPECT_TRUE(IsSrtpActive(channel2_));
    EXPECT_TRUE(IsRtcpMuxEnabled(channel1_));
    EXPECT_TRUE(IsRtcpMuxEnabled(channel2_));
    WaitForThreads();  // Wait for 'sending' flag go through network thread.
    SendCustomRtp1(kSsrc1, ++sequence_number1_1);
    WaitForThreads();
    EXPECT_TRUE(CheckCustomRtp2(kSsrc1, sequence_number1_1));

    // Send packets from callee and verify that it is received.
    SendCustomRtp2(kSsrc2, ++sequence_number2_2);
    WaitForThreads();
    EXPECT_TRUE(CheckCustomRtp1(kSsrc2, sequence_number2_2));

    // Complete call setup and ensure everything is still OK.
    EXPECT_TRUE(SendFinalAnswer());
    EXPECT_TRUE(IsSrtpActive(channel1_));
    EXPECT_TRUE(IsSrtpActive(channel2_));
    SendCustomRtp1(kSsrc1, ++sequence_number1_1);
    SendCustomRtp2(kSsrc2, ++sequence_number2_2);
    WaitForThreads();
    EXPECT_TRUE(CheckCustomRtp2(kSsrc1, sequence_number1_1));
    EXPECT_TRUE(CheckCustomRtp1(kSsrc2, sequence_number2_2));
  }

  // Test that we properly send RTP without SRTP from a thread.
  void SendRtpToRtpOnThread() {
    CreateChannels(0, 0);
    EXPECT_TRUE(SendInitiate());
    EXPECT_TRUE(SendAccept());
    ScopedCallThread send_rtp1([this] { SendRtp1(); });
    ScopedCallThread send_rtp2([this] { SendRtp2(); });
    Thread* involved_threads[] = {send_rtp1.thread(), send_rtp2.thread()};
    WaitForThreads(involved_threads);
    EXPECT_TRUE(CheckRtp1());
    EXPECT_TRUE(CheckRtp2());
  }

  // Test that the mediachannel retains its sending state after the transport
  // becomes non-writable.
  void SendWithWritabilityLoss() {
    CreateChannels(RTCP_MUX, RTCP_MUX);
    EXPECT_TRUE(SendInitiate());
    EXPECT_TRUE(SendAccept());
    EXPECT_TRUE(IsRtcpMuxEnabled(channel1_));
    EXPECT_TRUE(IsRtcpMuxEnabled(channel2_));
    SendRtp1();
    SendRtp2();
    WaitForThreads();
    EXPECT_TRUE(CheckRtp1());
    EXPECT_TRUE(CheckRtp2());

    // Lose writability, which should fail.
    SendTask(network_thread_,
             [this] { fake_rtp_dtls_transport1_->SetWritable(false); });
    SendRtp1();
    SendRtp2();
    WaitForThreads();
    EXPECT_TRUE(CheckRtp1());

    // Regain writability
    SendTask(network_thread_,
             [this] { fake_rtp_dtls_transport1_->SetWritable(true); });
    EXPECT_TRUE(media_send_channel1_impl()->sending());
    SendRtp1();
    SendRtp2();
    WaitForThreads();
    EXPECT_TRUE(CheckRtp1());
    EXPECT_TRUE(CheckRtp2());

    // Lose writability completely
    SendTask(network_thread_, [this] {
      bool asymmetric = true;
      fake_rtp_dtls_transport1_->SetDestination(nullptr, asymmetric);
    });
    EXPECT_TRUE(media_send_channel1_impl()->sending());

    // Should fail also.
    SendRtp1();
    SendRtp2();
    WaitForThreads();
    EXPECT_TRUE(CheckRtp1());

    // Gain writability back
    SendTask(network_thread_, [this] {
      bool asymmetric = true;
      fake_rtp_dtls_transport1_->SetDestination(fake_rtp_dtls_transport2_.get(),
                                                asymmetric);
    });
    EXPECT_TRUE(media_send_channel1_impl()->sending());
    SendRtp1();
    SendRtp2();
    WaitForThreads();
    EXPECT_TRUE(CheckRtp1());
    EXPECT_TRUE(CheckRtp2());
  }

  void SendBundleToBundle(std::span<const int, 2> pl_types,
                          bool rtcp_mux,
                          bool secure) {
    int sequence_number1_1 = 0, sequence_number2_2 = 0;
    // Only pl_type1 was added to the bundle filter for both `channel1_`
    // and `channel2_`.
    int pl_type1 = pl_types[0];
    int pl_type2 = pl_types[1];
    int flags = SSRC_MUX;
    if (secure)
      flags |= DTLS;
    if (rtcp_mux) {
      flags |= RTCP_MUX;
    }
    CreateChannels(flags, flags);
    EXPECT_TRUE(SendInitiate());
    EXPECT_TRUE(SendAccept());

    // Both channels can receive pl_type1 only.
    SendCustomRtp1(kSsrc1, ++sequence_number1_1, pl_type1);
    SendCustomRtp2(kSsrc2, ++sequence_number2_2, pl_type1);
    WaitForThreads();
    EXPECT_TRUE(CheckCustomRtp2(kSsrc1, sequence_number1_1, pl_type1));
    EXPECT_TRUE(CheckCustomRtp1(kSsrc2, sequence_number2_2, pl_type1));

    SendCustomRtp1(kSsrc3, ++sequence_number1_1, pl_type2);
    SendCustomRtp2(kSsrc4, ++sequence_number2_2, pl_type2);
    WaitForThreads();
    EXPECT_FALSE(CheckCustomRtp2(kSsrc3, sequence_number1_1, pl_type2));
    EXPECT_FALSE(CheckCustomRtp1(kSsrc4, sequence_number2_2, pl_type2));
  }

  void TestSetContentFailure() {
    CreateChannels(0, 0);

    std::unique_ptr<typename T::Content> content(
        CreateMediaContentWithStream(1));

    media_receive_channel1_impl()->set_fail_set_recv_codecs(true);
    EXPECT_FALSE(
        channel1_->SetLocalContent(content.get(), SdpType::kOffer).ok());
    EXPECT_FALSE(
        channel1_->SetLocalContent(content.get(), SdpType::kAnswer).ok());

    media_send_channel1_impl()->set_fail_set_send_codecs(true);
    EXPECT_FALSE(
        channel1_->SetRemoteContent(content.get(), SdpType::kOffer).ok());

    media_send_channel1_impl()->set_fail_set_send_codecs(true);
    EXPECT_FALSE(
        channel1_->SetRemoteContent(content.get(), SdpType::kAnswer).ok());
  }

  void TestSendTwoOffers() {
    CreateChannels(0, 0);

    std::unique_ptr<typename T::Content> content1(
        CreateMediaContentWithStream(1));
    EXPECT_TRUE(
        channel1_->SetLocalContent(content1.get(), SdpType::kOffer).ok());
    EXPECT_TRUE(media_send_channel1_impl()->HasSendStream(1));

    std::unique_ptr<typename T::Content> content2(
        CreateMediaContentWithStream(2));
    EXPECT_TRUE(
        channel1_->SetLocalContent(content2.get(), SdpType::kOffer).ok());
    EXPECT_FALSE(media_send_channel1_impl()->HasSendStream(1));
    EXPECT_TRUE(media_send_channel1_impl()->HasSendStream(2));
  }

  void TestReceiveTwoOffers() {
    CreateChannels(0, 0);

    std::unique_ptr<typename T::Content> content1(
        CreateMediaContentWithStream(1));
    EXPECT_TRUE(
        channel1_->SetRemoteContent(content1.get(), SdpType::kOffer).ok());
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(1));

    std::unique_ptr<typename T::Content> content2(
        CreateMediaContentWithStream(2));
    EXPECT_TRUE(
        channel1_->SetRemoteContent(content2.get(), SdpType::kOffer).ok());
    EXPECT_FALSE(media_receive_channel1_impl()->HasRecvStream(1));
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(2));
  }

  void TestSendPrAnswer() {
    CreateChannels(0, 0);

    // Receive offer
    std::unique_ptr<typename T::Content> content1(
        CreateMediaContentWithStream(1));
    EXPECT_TRUE(
        channel1_->SetRemoteContent(content1.get(), SdpType::kOffer).ok());
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(1));

    // Send PR answer
    std::unique_ptr<typename T::Content> content2(
        CreateMediaContentWithStream(2));
    EXPECT_TRUE(
        channel1_->SetLocalContent(content2.get(), SdpType::kPrAnswer).ok());
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(1));
    EXPECT_TRUE(media_send_channel1_impl()->HasSendStream(2));

    // Send answer
    std::unique_ptr<typename T::Content> content3(
        CreateMediaContentWithStream(3));
    EXPECT_TRUE(
        channel1_->SetLocalContent(content3.get(), SdpType::kAnswer).ok());
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(1));
    EXPECT_FALSE(media_send_channel1_impl()->HasSendStream(2));
    EXPECT_TRUE(media_send_channel1_impl()->HasSendStream(3));
  }

  void TestReceivePrAnswer() {
    CreateChannels(0, 0);

    // Send offer
    std::unique_ptr<typename T::Content> content1(
        CreateMediaContentWithStream(1));
    EXPECT_TRUE(
        channel1_->SetLocalContent(content1.get(), SdpType::kOffer).ok());
    EXPECT_TRUE(media_send_channel1_impl()->HasSendStream(1));

    // Receive PR answer
    std::unique_ptr<typename T::Content> content2(
        CreateMediaContentWithStream(2));
    EXPECT_TRUE(
        channel1_->SetRemoteContent(content2.get(), SdpType::kPrAnswer).ok());
    EXPECT_TRUE(media_send_channel1_impl()->HasSendStream(1));
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(2));

    // Receive answer
    std::unique_ptr<typename T::Content> content3(
        CreateMediaContentWithStream(3));
    EXPECT_TRUE(
        channel1_->SetRemoteContent(content3.get(), SdpType::kAnswer).ok());
    EXPECT_TRUE(media_send_channel1_impl()->HasSendStream(1));
    EXPECT_FALSE(media_receive_channel1_impl()->HasRecvStream(2));
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(3));
  }

  void TestOnTransportReadyToSend() {
    CreateChannels(0, 0);
    EXPECT_FALSE(media_send_channel1_impl()->ready_to_send());

    network_thread_->PostTask(
        [this] { channel1_->OnTransportReadyToSend(true); });
    WaitForThreads();
    EXPECT_TRUE(media_send_channel1_impl()->ready_to_send());

    network_thread_->PostTask(
        [this] { channel1_->OnTransportReadyToSend(false); });
    WaitForThreads();
    EXPECT_FALSE(media_send_channel1_impl()->ready_to_send());
  }

  bool SetRemoteContentWithBitrateLimit(int remote_limit) {
    typename T::Content content;
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    content.set_bandwidth(remote_limit);
    return channel1_->SetRemoteContent(&content, SdpType::kOffer, NULL);
  }

  RtpParameters BitrateLimitedParameters(std::optional<int> limit) {
    RtpParameters parameters;
    RtpEncodingParameters encoding;
    encoding.max_bitrate_bps = limit;
    parameters.encodings.push_back(encoding);
    return parameters;
  }

  void VerifyMaxBitrate(const RtpParameters& parameters,
                        std::optional<int> expected_bitrate) {
    EXPECT_EQ(1UL, parameters.encodings.size());
    EXPECT_EQ(expected_bitrate, parameters.encodings[0].max_bitrate_bps);
  }

  void DefaultMaxBitrateIsUnlimited() {
    CreateChannels(0, 0);
    EXPECT_TRUE(
        channel1_->SetLocalContent(&local_media_content1_, SdpType::kOffer)
            .ok());
    EXPECT_EQ(media_send_channel1_impl()->max_bps(), -1);
    VerifyMaxBitrate(
        channel1_->media_send_channel()->GetRtpSendParameters(kSsrc1),
        std::nullopt);
  }

  // Test that when a channel gets new RtpTransport with a call to
  // `SetRtpTransport`, the socket options from the old RtpTransport is merged
  // with the options on the new one.

  // For example, audio and video may use separate socket options, but initially
  // be unbundled, then later become bundled. When this happens, their preferred
  // socket options should be merged to the underlying transport they share.
  void SocketOptionsMergedOnSetTransport() {
    constexpr int kSndBufSize = 4000;
    constexpr int kRcvBufSize = 8000;

    CreateChannels(DTLS, DTLS);

    bool rcv_success, send_success;
    int rcv_buf, send_buf;
    SendTask(network_thread_, [&] {
      new_rtp_transport_ = CreateDtlsSrtpTransport(
          fake_rtp_dtls_transport2_.get(), fake_rtcp_dtls_transport2_.get());
      channel1_->SetOption(BaseChannel::ST_RTP, Socket::Option::OPT_SNDBUF,
                           kSndBufSize);
      channel2_->SetOption(BaseChannel::ST_RTP, Socket::Option::OPT_RCVBUF,
                           kRcvBufSize);
      channel1_->SetRtpTransport(new_rtp_transport_.get());
      send_success = fake_rtp_dtls_transport2_->GetOption(
          Socket::Option::OPT_SNDBUF, &send_buf);
      rcv_success = fake_rtp_dtls_transport2_->GetOption(
          Socket::Option::OPT_RCVBUF, &rcv_buf);
    });

    ASSERT_TRUE(send_success);
    EXPECT_EQ(kSndBufSize, send_buf);
    ASSERT_TRUE(rcv_success);
    EXPECT_EQ(kRcvBufSize, rcv_buf);
  }

  void CreateSimulcastContent(const std::vector<std::string>& rids,
                              typename T::Content* content) {
    std::vector<RidDescription> rid_descriptions;
    for (const std::string& name : rids) {
      rid_descriptions.push_back(RidDescription(name, RidDirection::kSend));
    }

    StreamParams stream;
    stream.set_rids(rid_descriptions);
    CreateContent(0, kPcmuCodec, kH264Codec, content);
    // This is for unified plan, so there can be only one StreamParams.
    content->mutable_streams().clear();
    content->AddStream(stream);
  }

  void VerifySimulcastStreamParams(const StreamParams& expected,
                                   const typename T::Channel* channel) {
    const std::vector<StreamParams>& streams = channel->local_streams();
    ASSERT_EQ(1u, streams.size());
    const StreamParams& result = streams[0];
    EXPECT_EQ(expected.rids(), result.rids());
    EXPECT_TRUE(result.has_ssrcs());
    EXPECT_EQ(expected.rids().size() * 2, result.ssrcs.size());
    std::vector<uint32_t> primary_ssrcs;
    result.GetPrimarySsrcs(&primary_ssrcs);
    EXPECT_EQ(expected.rids().size(), primary_ssrcs.size());
  }

  void TestUpdateLocalStreamsWithSimulcast() {
    CreateChannels(0, 0);
    typename T::Content content1, content2, content3;
    CreateSimulcastContent({"f", "h", "q"}, &content1);
    EXPECT_TRUE(channel1_->SetLocalContent(&content1, SdpType::kOffer).ok());
    VerifySimulcastStreamParams(content1.streams()[0], channel1_.get());
    StreamParams stream1 = channel1_->local_streams()[0];

    // Create a similar offer. SetLocalContent should not remove and add.
    CreateSimulcastContent({"f", "h", "q"}, &content2);
    EXPECT_TRUE(channel1_->SetLocalContent(&content2, SdpType::kOffer).ok());
    VerifySimulcastStreamParams(content2.streams()[0], channel1_.get());
    StreamParams stream2 = channel1_->local_streams()[0];
    // Check that the streams are identical (SSRCs didn't change).
    EXPECT_EQ(stream1, stream2);

    // Create third offer that has same RIDs in different order.
    CreateSimulcastContent({"f", "q", "h"}, &content3);
    EXPECT_TRUE(channel1_->SetLocalContent(&content3, SdpType::kOffer).ok());
    VerifySimulcastStreamParams(content3.streams()[0], channel1_.get());
  }

 protected:
  void WaitForThreads() { WaitForThreads(std::span<Thread*>()); }
  static void ProcessThreadQueue(Thread* thread) {
    RTC_DCHECK(thread->IsCurrent());
    while (!thread->empty()) {
      thread->ProcessMessages(0);
    }
  }
  void FlushCurrentThread() { main_thread_.Flush(); }
  void WaitForThreads(std::span<Thread*> threads) {
    // `threads` and current thread post packets to network thread.
    for (Thread* thread : threads) {
      SendTask(thread, [thread] { ProcessThreadQueue(thread); });
    }
    ProcessThreadQueue(Thread::Current());
    // Network thread move them around and post back to worker = current thread.
    if (!network_thread_->IsCurrent()) {
      SendTask(network_thread_,
               [this] { ProcessThreadQueue(network_thread_); });
    }
    // Worker thread = current Thread process received messages.
    ProcessThreadQueue(Thread::Current());
  }

  // Accessors that return the FakeMedia<type>SendChannel object.
  // Note that these depend on getting the object back that was
  // passed to the channel constructor.
  // T::MediaSendChannel is either FakeVoiceMediaSendChannel or
  // FakeVideoMediaSendChannel.
  typename T::MediaSendChannel* media_send_channel1_impl() {
    RTC_DCHECK(channel1_);
    return static_cast<typename T::MediaSendChannel*>(
        channel1_->media_send_channel());
  }

  typename T::MediaSendChannel* media_send_channel2_impl() {
    RTC_DCHECK(channel2_);
    RTC_DCHECK(channel2_->media_send_channel());
    return static_cast<typename T::MediaSendChannel*>(
        channel2_->media_send_channel());
  }
  typename T::MediaReceiveChannel* media_receive_channel1_impl() {
    RTC_DCHECK(channel1_);
    RTC_DCHECK(channel1_->media_receive_channel());
    return static_cast<typename T::MediaReceiveChannel*>(
        channel1_->media_receive_channel());
  }

  typename T::MediaReceiveChannel* media_receive_channel2_impl() {
    RTC_DCHECK(channel2_);
    RTC_DCHECK(channel2_->media_receive_channel());
    return static_cast<typename T::MediaReceiveChannel*>(
        channel2_->media_receive_channel());
  }

  void TestSetRtpTransportNullClearsReceiveSinks() {
    CreateChannels(0, 0);
    SendTask(channel1_->network_thread(),
             [this]() { channel1_->SetRtpTransport(nullptr); });
    EXPECT_TRUE(absl::c_linear_search(
        media_receive_channel1_impl()->clear_receive_sinks_calls(),
        std::nullopt));
  }

  void TestRegisterRtpDemuxerSinkPropagatesReceiveSsrcs() {
    CreateChannels(0, 0);
    typename T::Content content;
    CreateContent(0, kPcmuCodec, kH264Codec, &content);
    AddLegacyStreamInContent(kSsrc1, 0, &content);

    EXPECT_TRUE(channel1_->SetRemoteContent(&content, SdpType::kOffer).ok());
    EXPECT_GT(media_receive_channel1_impl()->receive_ssrcs_n().count(kSsrc1),
              0u);
  }

  void TestUpdateRemoteStreamsClearsRemovedSinks() {
    CreateChannels(0, 0);
    typename T::Content content1;
    CreateContent(0, kPcmuCodec, kH264Codec, &content1);
    AddLegacyStreamInContent(kSsrc1, 0, &content1);
    AddLegacyStreamInContent(kSsrc2, 0, &content1);

    EXPECT_TRUE(channel1_->SetRemoteContent(&content1, SdpType::kOffer).ok());
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(kSsrc1));
    EXPECT_TRUE(media_receive_channel1_impl()->HasRecvStream(kSsrc2));

    typename T::Content content2;
    CreateContent(0, kPcmuCodec, kH264Codec, &content2);
    AddLegacyStreamInContent(kSsrc2, 0, &content2);

    EXPECT_TRUE(channel1_->SetRemoteContent(&content2, SdpType::kOffer).ok());

    bool found_cleared_ssrc = false;
    for (const auto& call :
         media_receive_channel1_impl()->clear_receive_sinks_calls()) {
      if (call.has_value() && absl::c_linear_search(*call, kSsrc1)) {
        found_cleared_ssrc = true;
        break;
      }
    }
    EXPECT_TRUE(found_cleared_ssrc);
  }

  void TestUpdateRemoteStreamsClearsUnsignaledSinks() {
    CreateChannels(0, 0);

    typename T::Content content1;
    CreateContent(0, kPcmuCodec, kH264Codec, &content1);
    StreamParams unsignaled_stream;
    content1.AddStream(unsignaled_stream);

    EXPECT_TRUE(channel1_->SetRemoteContent(&content1, SdpType::kOffer).ok());

    media_receive_channel1_impl()->AddFakeUnsignaledSsrc(kSsrc3);

    typename T::Content content2;
    CreateContent(0, kPcmuCodec, kH264Codec, &content2);

    EXPECT_TRUE(channel1_->SetRemoteContent(&content2, SdpType::kOffer).ok());

    bool found_cleared_ssrc = false;
    for (const auto& call :
         media_receive_channel1_impl()->clear_receive_sinks_calls()) {
      if (call.has_value() && absl::c_linear_search(*call, kSsrc3)) {
        found_cleared_ssrc = true;
        break;
      }
    }
    EXPECT_TRUE(found_cleared_ssrc);
  }

  test::RunLoop main_thread_;
  const Environment env_ = CreateTestEnvironment();
  // TODO(pbos): Remove playout from all media channels and let renderers mute
  // themselves.
  const bool verify_playout_;
  scoped_refptr<PendingTaskSafetyFlag> network_thread_safety_ =
      PendingTaskSafetyFlag::CreateDetached();
  std::unique_ptr<Thread> network_thread_keeper_;
  Thread* network_thread_;
  std::unique_ptr<FakeDtlsTransport> fake_rtp_dtls_transport1_;
  std::unique_ptr<FakeDtlsTransport> fake_rtcp_dtls_transport1_;
  std::unique_ptr<FakeDtlsTransport> fake_rtp_dtls_transport2_;
  std::unique_ptr<FakeDtlsTransport> fake_rtcp_dtls_transport2_;
  std::unique_ptr<FakePacketTransport> fake_rtp_packet_transport1_;
  std::unique_ptr<FakePacketTransport> fake_rtcp_packet_transport1_;
  std::unique_ptr<FakePacketTransport> fake_rtp_packet_transport2_;
  std::unique_ptr<FakePacketTransport> fake_rtcp_packet_transport2_;
  std::unique_ptr<RtpTransportInternal> rtp_transport1_;
  std::unique_ptr<RtpTransportInternal> rtp_transport2_;
  std::unique_ptr<RtpTransportInternal> new_rtp_transport_;
  FakeMediaEngine media_engine_;
  std::unique_ptr<typename T::Channel> channel1_;
  std::unique_ptr<typename T::Channel> channel2_;
  typename T::Content local_media_content1_;
  typename T::Content local_media_content2_;
  typename T::Content remote_media_content1_;
  typename T::Content remote_media_content2_;
  // The RTP and RTCP packets to send in the tests.
  Buffer rtp_packet_;
  Buffer rtcp_packet_;
  CandidatePairInterface* last_selected_candidate_pair_;
  UniqueRandomIdGenerator ssrc_generator_;
};

template <>
std::unique_ptr<BaseChannel> ChannelTest<VoiceTraits>::CreateChannel(
    Thread* worker_thread,
    Thread* network_thread,
    std::unique_ptr<FakeVoiceMediaSendChannel> send_ch,
    std::unique_ptr<FakeVoiceMediaReceiveChannel> receive_ch,
    RtpTransportInternal* rtp_transport,
    int flags) {
  Thread* signaling_thread = Thread::Current();
  auto channel = std::make_unique<BaseChannel>(
      worker_thread, network_thread, signaling_thread, std::move(send_ch),
      std::move(receive_ch), kAudioMid, MediaType::AUDIO, (flags & DTLS) != 0,
      CryptoOptions(), &ssrc_generator_);
  SendTask(network_thread, [&]() {
    RTC_DCHECK_RUN_ON(channel->network_thread());
    channel->SetRtpTransport(rtp_transport);
  });
  return channel;
}

template <>
void ChannelTest<VoiceTraits>::CreateContent(int flags,
                                             const Codec& audio_codec,
                                             const Codec& video_codec,
                                             AudioContentDescription* audio) {
  audio->AddCodec(audio_codec);
  audio->set_rtcp_mux((flags & RTCP_MUX) != 0);
}

template <>
void ChannelTest<VoiceTraits>::CopyContent(
    const AudioContentDescription& source,
    AudioContentDescription* audio) {
  *audio = source;
}

template <>
void ChannelTest<VoiceTraits>::AddLegacyStreamInContent(
    uint32_t ssrc,
    int flags,
    AudioContentDescription* audio) {
  audio->AddLegacyStream(ssrc);
}

class VoiceChannelSingleThreadTest : public ChannelTest<VoiceTraits> {
 public:
  using Base = ChannelTest<VoiceTraits>;
  VoiceChannelSingleThreadTest()
      : Base(true, kPcmuFrame, kFakeRtcpReport, NetworkIsWorker::Yes) {}
};

class VoiceChannelDoubleThreadTest : public ChannelTest<VoiceTraits> {
 public:
  using Base = ChannelTest<VoiceTraits>;
  VoiceChannelDoubleThreadTest()
      : Base(true, kPcmuFrame, kFakeRtcpReport, NetworkIsWorker::No) {}
};

class VoiceChannelWithEncryptedRtpHeaderExtensionsSingleThreadTest
    : public ChannelTest<VoiceTraits> {
 public:
  using Base = ChannelTest<VoiceTraits>;
  VoiceChannelWithEncryptedRtpHeaderExtensionsSingleThreadTest()
      : Base(true,
             kPcmuFrameWithExtensions,
             kFakeRtcpReport,
             NetworkIsWorker::Yes) {}
};

class VoiceChannelWithEncryptedRtpHeaderExtensionsDoubleThreadTest
    : public ChannelTest<VoiceTraits> {
 public:
  using Base = ChannelTest<VoiceTraits>;
  VoiceChannelWithEncryptedRtpHeaderExtensionsDoubleThreadTest()
      : Base(true,
             kPcmuFrameWithExtensions,
             kFakeRtcpReport,
             NetworkIsWorker::No) {}
};

// override to add NULL parameter
template <>
std::unique_ptr<BaseChannel> ChannelTest<VideoTraits>::CreateChannel(
    Thread* worker_thread,
    Thread* network_thread,
    std::unique_ptr<FakeVideoMediaSendChannel> send_ch,
    std::unique_ptr<FakeVideoMediaReceiveChannel> receive_ch,
    RtpTransportInternal* rtp_transport,
    int flags) {
  Thread* signaling_thread = Thread::Current();
  auto channel = std::make_unique<BaseChannel>(
      worker_thread, network_thread, signaling_thread, std::move(send_ch),
      std::move(receive_ch), kVideoMid, MediaType::VIDEO, (flags & DTLS) != 0,
      CryptoOptions(), &ssrc_generator_);
  SendTask(network_thread, [&]() {
    RTC_DCHECK_RUN_ON(channel->network_thread());
    channel->SetRtpTransport(rtp_transport);
  });
  return channel;
}

template <>
void ChannelTest<VideoTraits>::CreateContent(int flags,
                                             const Codec& audio_codec,
                                             const Codec& video_codec,
                                             VideoContentDescription* video) {
  video->AddCodec(video_codec);
  video->set_rtcp_mux((flags & RTCP_MUX) != 0);
}

template <>
void ChannelTest<VideoTraits>::CopyContent(
    const VideoContentDescription& source,
    VideoContentDescription* video) {
  *video = source;
}

template <>
void ChannelTest<VideoTraits>::AddLegacyStreamInContent(
    uint32_t ssrc,
    int flags,
    VideoContentDescription* video) {
  video->AddLegacyStream(ssrc);
}

class VideoChannelSingleThreadTest : public ChannelTest<VideoTraits> {
 public:
  using Base = ChannelTest<VideoTraits>;
  VideoChannelSingleThreadTest()
      : Base(false, kH264Packet, kFakeRtcpReport, NetworkIsWorker::Yes) {}
};

class VideoChannelDoubleThreadTest : public ChannelTest<VideoTraits> {
 public:
  using Base = ChannelTest<VideoTraits>;
  VideoChannelDoubleThreadTest()
      : Base(false, kH264Packet, kFakeRtcpReport, NetworkIsWorker::No) {}
};

TEST_F(VoiceChannelSingleThreadTest, TestInit) {
  Base::TestInit();
  EXPECT_FALSE(media_send_channel1_impl()->IsStreamMuted(0));
  EXPECT_TRUE(media_send_channel1_impl()->dtmf_info_queue().empty());
}

TEST_F(VoiceChannelSingleThreadTest, TestDeinit) {
  Base::TestDeinit();
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContents) {
  Base::TestSetContents();
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContentsExtmapAllowMixedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/true);
}

TEST_F(VoiceChannelSingleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/false);
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContentsExtmapAllowMixedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/true);
}

TEST_F(VoiceChannelSingleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/false);
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContentsNullOffer) {
  Base::TestSetContentsNullOffer();
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContentsRtcpMux) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContentsRtcpMuxWithPrAnswer) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContentsRtcpReducedSize) {
  Base::TestSetContentsRtcpReducedSize();
}

TEST_F(VoiceChannelSingleThreadTest, TestChangeStreamParamsInContent) {
  Base::TestChangeStreamParamsInContent();
}

TEST_F(VoiceChannelSingleThreadTest, TestPlayoutAndSendingStates) {
  Base::TestPlayoutAndSendingStates();
}

TEST_F(VoiceChannelSingleThreadTest, TestMediaContentDirection) {
  Base::TestMediaContentDirection();
}

TEST_F(VoiceChannelSingleThreadTest, TestCallSetup) {
  Base::TestCallSetup();
}

TEST_F(VoiceChannelSingleThreadTest, SendRtpToRtp) {
  Base::SendRtpToRtp();
}

TEST_F(VoiceChannelSingleThreadTest, SendDtlsSrtpToDtlsSrtp) {
  Base::SendDtlsSrtpToDtlsSrtp(0, 0);
}

TEST_F(VoiceChannelSingleThreadTest, SendDtlsSrtpToDtlsSrtpRtcpMux) {
  Base::SendDtlsSrtpToDtlsSrtp(RTCP_MUX, RTCP_MUX);
}

TEST_F(VoiceChannelSingleThreadTest, SendEarlyMediaUsingRtcpMuxSrtp) {
  Base::SendEarlyMediaUsingRtcpMuxSrtp();
}

TEST_F(VoiceChannelSingleThreadTest, SendRtpToRtpOnThread) {
  Base::SendRtpToRtpOnThread();
}

TEST_F(VoiceChannelSingleThreadTest, SendWithWritabilityLoss) {
  Base::SendWithWritabilityLoss();
}

TEST_F(VoiceChannelSingleThreadTest, TestSetContentFailure) {
  Base::TestSetContentFailure();
}

TEST_F(VoiceChannelSingleThreadTest, TestSendTwoOffers) {
  Base::TestSendTwoOffers();
}

TEST_F(VoiceChannelSingleThreadTest, TestReceiveTwoOffers) {
  Base::TestReceiveTwoOffers();
}

TEST_F(VoiceChannelSingleThreadTest, TestSendPrAnswer) {
  Base::TestSendPrAnswer();
}

TEST_F(VoiceChannelSingleThreadTest, TestReceivePrAnswer) {
  Base::TestReceivePrAnswer();
}

TEST_F(VoiceChannelSingleThreadTest, TestOnTransportReadyToSend) {
  Base::TestOnTransportReadyToSend();
}

TEST_F(VoiceChannelSingleThreadTest, SendBundleToBundle) {
  Base::SendBundleToBundle(kAudioPts, false, false);
}

TEST_F(VoiceChannelSingleThreadTest, SendBundleToBundleSecure) {
  Base::SendBundleToBundle(kAudioPts, false, true);
}

TEST_F(VoiceChannelSingleThreadTest, SendBundleToBundleWithRtcpMux) {
  Base::SendBundleToBundle(kAudioPts, true, false);
}

TEST_F(VoiceChannelSingleThreadTest, SendBundleToBundleWithRtcpMuxSecure) {
  Base::SendBundleToBundle(kAudioPts, true, true);
}

TEST_F(VoiceChannelSingleThreadTest, DefaultMaxBitrateIsUnlimited) {
  Base::DefaultMaxBitrateIsUnlimited();
}

TEST_F(VoiceChannelSingleThreadTest, SocketOptionsMergedOnSetTransport) {
  Base::SocketOptionsMergedOnSetTransport();
}

TEST_F(VoiceChannelSingleThreadTest, RemovesExtensionNotPresentInRemoteAnswer) {
  Base::TestRemovesExtensionNotPresentInRemoteAnswer();
}

TEST_F(VoiceChannelSingleThreadTest, RemovesExtensionNotPresentInLocalAnswer) {
  Base::TestRemovesExtensionNotPresentInLocalAnswer();
}

TEST_F(VoiceChannelSingleThreadTest, DuplicateRtpHeaderExtensionIds) {
  Base::TestDuplicateRtpHeaderExtensionIds();
}

#if GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST_F(VoiceChannelSingleThreadTest,
       RtpHeaderExtensionIdOutOfRangeHighDeathTest) {
#if RTC_DCHECK_IS_ON
  // Note - EXPECT_DEBUG_DEATH does not work as expected here.
  EXPECT_DEATH(Base::TestRtpHeaderExtensionIdOutOfRangeHigh(), "Check failed:");
#else
  Base::TestRtpHeaderExtensionIdOutOfRangeHigh();
#endif
}

TEST_F(VoiceChannelSingleThreadTest,
       RtpHeaderExtensionIdOutOfRangeLowDeathTest) {
#if RTC_DCHECK_IS_ON
  // Note - EXPECT_DEBUG_DEATH does not work as expected here.
  EXPECT_DEATH(Base::TestRtpHeaderExtensionIdOutOfRangeLow(), "Check failed:");
#else
  Base::TestRtpHeaderExtensionIdOutOfRangeLow();
#endif
}
#endif

TEST_F(VoiceChannelSingleThreadTest, RtpHeaderExtensionIdReassignment) {
  Base::TestRtpHeaderExtensionIdReassignment();
}

TEST_F(VoiceChannelSingleThreadTest,
       TestRtpHeaderExtensionIdHistoryReassignment) {
  Base::TestRtpHeaderExtensionIdHistoryReassignment();
}

TEST_F(VoiceChannelSingleThreadTest,
       TestSetRtpTransportNullClearsReceiveSinks) {
  Base::TestSetRtpTransportNullClearsReceiveSinks();
}

TEST_F(VoiceChannelSingleThreadTest,
       TestRegisterRtpDemuxerSinkPropagatesReceiveSsrcs) {
  Base::TestRegisterRtpDemuxerSinkPropagatesReceiveSsrcs();
}

TEST_F(VoiceChannelSingleThreadTest,
       TestUpdateRemoteStreamsClearsRemovedSinks) {
  Base::TestUpdateRemoteStreamsClearsRemovedSinks();
}

TEST_F(VoiceChannelSingleThreadTest,
       TestUpdateRemoteStreamsClearsUnsignaledSinks) {
  Base::TestUpdateRemoteStreamsClearsUnsignaledSinks();
}

// The send streams are added with the local offer, but the send parameters are
// only applied with the remote answer. Test that the MID is set with the offer,
// so that the send streams are created with it.
TEST_F(VoiceChannelSingleThreadTest, SetsMidOnSendChannelWithLocalOffer) {
  CreateChannels(0, 0);
  EXPECT_TRUE(
      channel1_->SetLocalContent(&local_media_content1_, SdpType::kOffer).ok());
  EXPECT_EQ(media_send_channel1_impl()->send_streams().size(), 1u);
  EXPECT_EQ(media_send_channel1_impl()->mid(), kAudioMid);
}

// VoiceChannelDoubleThreadTest
TEST_F(VoiceChannelDoubleThreadTest, TestInit) {
  Base::TestInit();
  EXPECT_FALSE(media_send_channel1_impl()->IsStreamMuted(0));
  EXPECT_TRUE(media_send_channel1_impl()->dtmf_info_queue().empty());
}

TEST_F(VoiceChannelDoubleThreadTest, TestDeinit) {
  Base::TestDeinit();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContents) {
  Base::TestSetContents();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContentsExtmapAllowMixedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/true);
}

TEST_F(VoiceChannelDoubleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/false);
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContentsExtmapAllowMixedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/true);
}

TEST_F(VoiceChannelDoubleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/false);
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContentsNullOffer) {
  Base::TestSetContentsNullOffer();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContentsRtcpMux) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContentsRtcpMuxWithPrAnswer) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContentsRtcpReducedSize) {
  Base::TestSetContentsRtcpReducedSize();
}

TEST_F(VoiceChannelDoubleThreadTest, TestChangeStreamParamsInContent) {
  Base::TestChangeStreamParamsInContent();
}

TEST_F(VoiceChannelDoubleThreadTest, TestUpdateRemoteStreamsRaceWithRtpPacket) {
  Base::TestUpdateRemoteStreamsRaceWithRtpPacket();
}

TEST_F(VoiceChannelDoubleThreadTest, TestPlayoutAndSendingStates) {
  Base::TestPlayoutAndSendingStates();
}

TEST_F(VoiceChannelDoubleThreadTest, TestMediaContentDirection) {
  Base::TestMediaContentDirection();
}

TEST_F(VoiceChannelDoubleThreadTest, TestCallSetup) {
  Base::TestCallSetup();
}

TEST_F(VoiceChannelDoubleThreadTest, SendRtpToRtp) {
  Base::SendRtpToRtp();
}

TEST_F(VoiceChannelDoubleThreadTest, SendDtlsSrtpToDtlsSrtp) {
  Base::SendDtlsSrtpToDtlsSrtp(0, 0);
}

TEST_F(VoiceChannelDoubleThreadTest, SendDtlsSrtpToDtlsSrtpRtcpMux) {
  Base::SendDtlsSrtpToDtlsSrtp(RTCP_MUX, RTCP_MUX);
}

TEST_F(VoiceChannelDoubleThreadTest, SendEarlyMediaUsingRtcpMuxSrtp) {
  Base::SendEarlyMediaUsingRtcpMuxSrtp();
}

TEST_F(VoiceChannelDoubleThreadTest, SendRtpToRtpOnThread) {
  Base::SendRtpToRtpOnThread();
}

TEST_F(VoiceChannelDoubleThreadTest, SendWithWritabilityLoss) {
  Base::SendWithWritabilityLoss();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSetContentFailure) {
  Base::TestSetContentFailure();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSendTwoOffers) {
  Base::TestSendTwoOffers();
}

TEST_F(VoiceChannelDoubleThreadTest, TestReceiveTwoOffers) {
  Base::TestReceiveTwoOffers();
}

TEST_F(VoiceChannelDoubleThreadTest, TestSendPrAnswer) {
  Base::TestSendPrAnswer();
}

TEST_F(VoiceChannelDoubleThreadTest, TestReceivePrAnswer) {
  Base::TestReceivePrAnswer();
}

TEST_F(VoiceChannelDoubleThreadTest, TestOnTransportReadyToSend) {
  Base::TestOnTransportReadyToSend();
}

TEST_F(VoiceChannelDoubleThreadTest, SendBundleToBundle) {
  Base::SendBundleToBundle(kAudioPts, false, false);
}

TEST_F(VoiceChannelDoubleThreadTest, SendBundleToBundleSecure) {
  Base::SendBundleToBundle(kAudioPts, false, true);
}

TEST_F(VoiceChannelDoubleThreadTest, SendBundleToBundleWithRtcpMux) {
  Base::SendBundleToBundle(kAudioPts, true, false);
}

TEST_F(VoiceChannelDoubleThreadTest, SendBundleToBundleWithRtcpMuxSecure) {
  Base::SendBundleToBundle(kAudioPts, true, true);
}

TEST_F(VoiceChannelDoubleThreadTest, DefaultMaxBitrateIsUnlimited) {
  Base::DefaultMaxBitrateIsUnlimited();
}

TEST_F(VoiceChannelDoubleThreadTest, SocketOptionsMergedOnSetTransport) {
  Base::SocketOptionsMergedOnSetTransport();
}

// VideoChannelSingleThreadTest
TEST_F(VideoChannelSingleThreadTest, TestInit) {
  Base::TestInit();
}

TEST_F(VideoChannelSingleThreadTest, TestDeinit) {
  Base::TestDeinit();
}

TEST_F(VideoChannelSingleThreadTest, TestSetContents) {
  Base::TestSetContents();
}

TEST_F(VideoChannelSingleThreadTest, TestSetContentsExtmapAllowMixedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/true);
}

TEST_F(VideoChannelSingleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/false);
}

TEST_F(VideoChannelSingleThreadTest, TestSetContentsExtmapAllowMixedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/true);
}

TEST_F(VideoChannelSingleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/false);
}

TEST_F(VideoChannelSingleThreadTest, TestSetContentsNullOffer) {
  Base::TestSetContentsNullOffer();
}

TEST_F(VideoChannelSingleThreadTest, TestSetContentsRtcpMux) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VideoChannelSingleThreadTest, TestSetContentsRtcpMuxWithPrAnswer) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VideoChannelSingleThreadTest, TestChangeStreamParamsInContent) {
  Base::TestChangeStreamParamsInContent();
}

TEST_F(VideoChannelSingleThreadTest, TestPlayoutAndSendingStates) {
  Base::TestPlayoutAndSendingStates();
}

TEST_F(VideoChannelSingleThreadTest, TestMediaContentDirection) {
  Base::TestMediaContentDirection();
}

TEST_F(VideoChannelSingleThreadTest, TestCallSetup) {
  Base::TestCallSetup();
}

TEST_F(VideoChannelSingleThreadTest, SendRtpToRtp) {
  Base::SendRtpToRtp();
}

TEST_F(VideoChannelSingleThreadTest, SendDtlsSrtpToDtlsSrtp) {
  Base::SendDtlsSrtpToDtlsSrtp(0, 0);
}

TEST_F(VideoChannelSingleThreadTest, SendDtlsSrtpToDtlsSrtpRtcpMux) {
  Base::SendDtlsSrtpToDtlsSrtp(RTCP_MUX, RTCP_MUX);
}

TEST_F(VideoChannelSingleThreadTest, SendEarlyMediaUsingRtcpMuxSrtp) {
  Base::SendEarlyMediaUsingRtcpMuxSrtp();
}

TEST_F(VideoChannelSingleThreadTest, SendRtpToRtpOnThread) {
  Base::SendRtpToRtpOnThread();
}

TEST_F(VideoChannelSingleThreadTest, SendWithWritabilityLoss) {
  Base::SendWithWritabilityLoss();
}

TEST_F(VideoChannelSingleThreadTest, TestSetContentFailure) {
  Base::TestSetContentFailure();
}

TEST_F(VideoChannelSingleThreadTest, TestSendTwoOffers) {
  Base::TestSendTwoOffers();
}

TEST_F(VideoChannelSingleThreadTest, TestReceiveTwoOffers) {
  Base::TestReceiveTwoOffers();
}

TEST_F(VideoChannelSingleThreadTest, TestSendPrAnswer) {
  Base::TestSendPrAnswer();
}

TEST_F(VideoChannelSingleThreadTest, TestReceivePrAnswer) {
  Base::TestReceivePrAnswer();
}

TEST_F(VideoChannelSingleThreadTest, SendBundleToBundle) {
  Base::SendBundleToBundle(kVideoPts, false, false);
}

TEST_F(VideoChannelSingleThreadTest, SendBundleToBundleSecure) {
  Base::SendBundleToBundle(kVideoPts, false, true);
}

TEST_F(VideoChannelSingleThreadTest, SendBundleToBundleWithRtcpMux) {
  Base::SendBundleToBundle(kVideoPts, true, false);
}

TEST_F(VideoChannelSingleThreadTest, SendBundleToBundleWithRtcpMuxSecure) {
  Base::SendBundleToBundle(kVideoPts, true, true);
}

TEST_F(VideoChannelSingleThreadTest, TestOnTransportReadyToSend) {
  Base::TestOnTransportReadyToSend();
}

TEST_F(VideoChannelSingleThreadTest, DefaultMaxBitrateIsUnlimited) {
  Base::DefaultMaxBitrateIsUnlimited();
}

TEST_F(VideoChannelSingleThreadTest, SocketOptionsMergedOnSetTransport) {
  Base::SocketOptionsMergedOnSetTransport();
}

TEST_F(VideoChannelSingleThreadTest, UpdateLocalStreamsWithSimulcast) {
  Base::TestUpdateLocalStreamsWithSimulcast();
}

TEST_F(VideoChannelSingleThreadTest, RemovesExtensionNotPresentInRemoteAnswer) {
  Base::TestRemovesExtensionNotPresentInRemoteAnswer();
}

TEST_F(VideoChannelSingleThreadTest, RemovesExtensionNotPresentInLocalAnswer) {
  Base::TestRemovesExtensionNotPresentInLocalAnswer();
}

TEST_F(VideoChannelSingleThreadTest, DuplicateRtpHeaderExtensionIds) {
  Base::TestDuplicateRtpHeaderExtensionIds();
}

#if GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST_F(VideoChannelSingleThreadTest,
       RtpHeaderExtensionIdOutOfRangeHighDeathTest) {
#if RTC_DCHECK_IS_ON
  // Note - EXPECT_DEBUG_DEATH does not work as expected here.
  EXPECT_DEATH(Base::TestRtpHeaderExtensionIdOutOfRangeHigh(), "Check failed:");
#else
  Base::TestRtpHeaderExtensionIdOutOfRangeHigh();
#endif
}

TEST_F(VideoChannelSingleThreadTest,
       RtpHeaderExtensionIdOutOfRangeLowDeathTest) {
#if RTC_DCHECK_IS_ON
  // Note - EXPECT_DEBUG_DEATH does not work as expected here.
  EXPECT_DEATH(Base::TestRtpHeaderExtensionIdOutOfRangeLow(), "Check failed:");
#else
  Base::TestRtpHeaderExtensionIdOutOfRangeLow();
#endif
}
#endif

TEST_F(VideoChannelSingleThreadTest, RtpHeaderExtensionIdReassignment) {
  Base::TestRtpHeaderExtensionIdReassignment();
}

TEST_F(VideoChannelSingleThreadTest,
       TestRtpHeaderExtensionIdHistoryReassignment) {
  Base::TestRtpHeaderExtensionIdHistoryReassignment();
}

TEST_F(VideoChannelSingleThreadTest, TestSetLocalOfferWithPacketization) {
  const Codec kVp8Codec = CreateVideoCodec(97, "VP8");
  Codec vp9_codec = CreateVideoCodec(98, "VP9");
  vp9_codec.packetization = kPacketizationParamRaw;
  VideoContentDescription video;
  video.set_codecs({kVp8Codec, vp9_codec});

  CreateChannels(0, 0);

  EXPECT_TRUE(channel1_->SetLocalContent(&video, SdpType::kOffer).ok());
  EXPECT_THAT(media_send_channel1_impl()->send_codecs(), testing::IsEmpty());
  ASSERT_THAT(media_receive_channel1_impl()->recv_codecs(), testing::SizeIs(2));
  EXPECT_TRUE(
      media_receive_channel1_impl()->recv_codecs()[0].Matches(kVp8Codec));
  EXPECT_EQ(media_receive_channel1_impl()->recv_codecs()[0].packetization,
            std::nullopt);
  EXPECT_TRUE(
      media_receive_channel1_impl()->recv_codecs()[1].Matches(vp9_codec));
  EXPECT_EQ(media_receive_channel1_impl()->recv_codecs()[1].packetization,
            kPacketizationParamRaw);
}

TEST_F(VideoChannelSingleThreadTest, TestSetRemoteOfferWithPacketization) {
  const Codec kVp8Codec = CreateVideoCodec(97, "VP8");
  Codec vp9_codec = CreateVideoCodec(98, "VP9");
  vp9_codec.packetization = kPacketizationParamRaw;
  VideoContentDescription video;
  video.set_codecs({kVp8Codec, vp9_codec});

  CreateChannels(0, 0);

  EXPECT_TRUE(channel1_->SetRemoteContent(&video, SdpType::kOffer).ok());
  EXPECT_THAT(media_receive_channel1_impl()->recv_codecs(), testing::IsEmpty());
  ASSERT_THAT(media_send_channel1_impl()->send_codecs(), testing::SizeIs(2));
  EXPECT_TRUE(media_send_channel1_impl()->send_codecs()[0].Matches(kVp8Codec));
  EXPECT_EQ(media_send_channel1_impl()->send_codecs()[0].packetization,
            std::nullopt);
  EXPECT_TRUE(media_send_channel1_impl()->send_codecs()[1].Matches(vp9_codec));
  EXPECT_EQ(media_send_channel1_impl()->send_codecs()[1].packetization,
            kPacketizationParamRaw);
}

TEST_F(VideoChannelSingleThreadTest, TestSetAnswerWithPacketization) {
  const Codec kVp8Codec = CreateVideoCodec(97, "VP8");
  Codec vp9_codec = CreateVideoCodec(98, "VP9");
  vp9_codec.packetization = kPacketizationParamRaw;
  VideoContentDescription video;
  video.set_codecs({kVp8Codec, vp9_codec});

  CreateChannels(0, 0);

  EXPECT_TRUE(channel1_->SetLocalContent(&video, SdpType::kOffer).ok());
  EXPECT_TRUE(channel1_->SetRemoteContent(&video, SdpType::kAnswer).ok());
  ASSERT_THAT(media_receive_channel1_impl()->recv_codecs(), testing::SizeIs(2));
  EXPECT_TRUE(
      media_receive_channel1_impl()->recv_codecs()[0].Matches(kVp8Codec));
  EXPECT_EQ(media_receive_channel1_impl()->recv_codecs()[0].packetization,
            std::nullopt);
  EXPECT_TRUE(
      media_receive_channel1_impl()->recv_codecs()[1].Matches(vp9_codec));
  EXPECT_EQ(media_receive_channel1_impl()->recv_codecs()[1].packetization,
            kPacketizationParamRaw);
  EXPECT_THAT(media_send_channel1_impl()->send_codecs(), testing::SizeIs(2));
  EXPECT_TRUE(media_send_channel1_impl()->send_codecs()[0].Matches(kVp8Codec));
  EXPECT_EQ(media_send_channel1_impl()->send_codecs()[0].packetization,
            std::nullopt);
  EXPECT_TRUE(media_send_channel1_impl()->send_codecs()[1].Matches(vp9_codec));
  EXPECT_EQ(media_send_channel1_impl()->send_codecs()[1].packetization,
            kPacketizationParamRaw);
}

TEST_F(VideoChannelSingleThreadTest, TestSetLocalAnswerWithoutPacketization) {
  const Codec kLocalCodec = CreateVideoCodec(98, "VP8");
  Codec remote_codec = CreateVideoCodec(99, "VP8");
  remote_codec.packetization = kPacketizationParamRaw;
  VideoContentDescription local_video;
  local_video.set_codecs({kLocalCodec});
  VideoContentDescription remote_video;
  remote_video.set_codecs({remote_codec});

  CreateChannels(0, 0);

  EXPECT_TRUE(channel1_->SetRemoteContent(&remote_video, SdpType::kOffer).ok());
  EXPECT_TRUE(channel1_->SetLocalContent(&local_video, SdpType::kAnswer).ok());
  ASSERT_THAT(media_receive_channel1_impl()->recv_codecs(), testing::SizeIs(1));
  EXPECT_EQ(media_receive_channel1_impl()->recv_codecs()[0].packetization,
            std::nullopt);
  ASSERT_THAT(media_send_channel1_impl()->send_codecs(), testing::SizeIs(1));
  EXPECT_EQ(media_send_channel1_impl()->send_codecs()[0].packetization,
            std::nullopt);
}

TEST_F(VideoChannelSingleThreadTest, TestSetRemoteAnswerWithoutPacketization) {
  Codec local_codec = CreateVideoCodec(98, "VP8");
  local_codec.packetization = kPacketizationParamRaw;
  const Codec kRemoteCodec = CreateVideoCodec(99, "VP8");
  VideoContentDescription local_video;
  local_video.set_codecs({local_codec});
  VideoContentDescription remote_video;
  remote_video.set_codecs({kRemoteCodec});

  CreateChannels(0, 0);

  EXPECT_TRUE(channel1_->SetLocalContent(&local_video, SdpType::kOffer).ok());
  EXPECT_TRUE(
      channel1_->SetRemoteContent(&remote_video, SdpType::kAnswer).ok());
  ASSERT_THAT(media_receive_channel1_impl()->recv_codecs(), testing::SizeIs(1));
  EXPECT_EQ(media_receive_channel1_impl()->recv_codecs()[0].packetization,
            std::nullopt);
  ASSERT_THAT(media_send_channel1_impl()->send_codecs(), testing::SizeIs(1));
  EXPECT_EQ(media_send_channel1_impl()->send_codecs()[0].packetization,
            std::nullopt);
}

TEST_F(VideoChannelSingleThreadTest,
       TestSetRemoteAnswerWithInvalidPacketization) {
  Codec local_codec = CreateVideoCodec(98, "VP8");
  local_codec.packetization = kPacketizationParamRaw;
  Codec remote_codec = CreateVideoCodec(99, "VP8");
  remote_codec.packetization = "unknownpacketizationattributevalue";
  VideoContentDescription local_video;
  local_video.set_codecs({local_codec});
  VideoContentDescription remote_video;
  remote_video.set_codecs({remote_codec});

  CreateChannels(0, 0);

  EXPECT_TRUE(channel1_->SetLocalContent(&local_video, SdpType::kOffer).ok());
  EXPECT_FALSE(
      channel1_->SetRemoteContent(&remote_video, SdpType::kAnswer).ok());
  ASSERT_THAT(media_receive_channel1_impl()->recv_codecs(), testing::SizeIs(1));
  EXPECT_EQ(media_receive_channel1_impl()->recv_codecs()[0].packetization,
            kPacketizationParamRaw);
  EXPECT_THAT(media_send_channel1_impl()->send_codecs(), testing::IsEmpty());
}

TEST_F(VideoChannelSingleThreadTest,
       TestSetLocalAnswerWithInvalidPacketization) {
  Codec local_codec = CreateVideoCodec(98, "VP8");
  local_codec.packetization = kPacketizationParamRaw;
  const Codec kRemoteCodec = CreateVideoCodec(99, "VP8");
  VideoContentDescription local_video;
  local_video.set_codecs({local_codec});
  VideoContentDescription remote_video;
  remote_video.set_codecs({kRemoteCodec});

  CreateChannels(0, 0);

  EXPECT_TRUE(channel1_->SetRemoteContent(&remote_video, SdpType::kOffer).ok());
  EXPECT_FALSE(channel1_->SetLocalContent(&local_video, SdpType::kAnswer).ok());
  EXPECT_THAT(media_receive_channel1_impl()->recv_codecs(), testing::IsEmpty());
  ASSERT_THAT(media_send_channel1_impl()->send_codecs(), testing::SizeIs(1));
  EXPECT_EQ(media_send_channel1_impl()->send_codecs()[0].packetization,
            std::nullopt);
}

TEST_F(VideoChannelSingleThreadTest,
       StopsPacketizationVerificationWhenMatchIsFoundInRemoteAnswer) {
  Codec vp8_foo = CreateVideoCodec(96, "VP8");
  vp8_foo.packetization = "foo";
  Codec vp8_bar = CreateVideoCodec(97, "VP8");
  vp8_bar.packetization = "bar";
  Codec vp9 = CreateVideoCodec(98, "VP9");
  Codec vp9_foo = CreateVideoCodec(99, "VP9");
  vp9_foo.packetization = "bar";
  VideoContentDescription local;
  local.set_codecs({vp8_foo, vp8_bar, vp9_foo});
  VideoContentDescription remote;
  remote.set_codecs({vp8_foo, vp9});

  CreateChannels(0, 0);
  ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kOffer).ok());
  ASSERT_TRUE(channel1_->SetRemoteContent(&remote, SdpType::kAnswer).ok());

  EXPECT_THAT(
      media_receive_channel1_impl()->recv_codecs(),
      ElementsAre(
          AllOf(Field(&Codec::id, 96), Field(&Codec::packetization, "foo")),
          AllOf(Field(&Codec::id, 97), Field(&Codec::packetization, "bar")),
          AllOf(Field(&Codec::id, 99),
                Field(&Codec::packetization, std::nullopt))));
  EXPECT_THAT(media_send_channel1_impl()->send_codecs(),
              ElementsAre(AllOf(Field(&Codec::id, 96),
                                Field(&Codec::packetization, "foo")),
                          AllOf(Field(&Codec::id, 98),
                                Field(&Codec::packetization, std::nullopt))));
}

TEST_F(VideoChannelSingleThreadTest,
       StopsPacketizationVerificationWhenMatchIsFoundInLocalAnswer) {
  Codec vp8_foo = CreateVideoCodec(96, "VP8");
  vp8_foo.packetization = "foo";
  Codec vp8_bar = CreateVideoCodec(97, "VP8");
  vp8_bar.packetization = "bar";
  Codec vp9 = CreateVideoCodec(98, "VP9");
  Codec vp9_foo = CreateVideoCodec(99, "VP9");
  vp9_foo.packetization = "bar";
  VideoContentDescription local;
  local.set_codecs({vp8_foo, vp9});
  VideoContentDescription remote;
  remote.set_codecs({vp8_foo, vp8_bar, vp9_foo});

  CreateChannels(0, 0);
  ASSERT_TRUE(channel1_->SetRemoteContent(&remote, SdpType::kOffer).ok());
  ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kAnswer).ok());

  EXPECT_THAT(
      media_receive_channel1_impl()->recv_codecs(),
      ElementsAre(
          AllOf(Field("id", &Codec::id, 96),
                Field("packetization", &Codec::packetization, "foo")),
          AllOf(Field("id", &Codec::id, 98),
                Field("packetization", &Codec::packetization, std::nullopt))));
  EXPECT_THAT(
      media_send_channel1_impl()->send_codecs(),
      ElementsAre(
          AllOf(Field("id", &Codec::id, 96),
                Field("packetization", &Codec::packetization, "foo")),
          AllOf(Field("id", &Codec::id, 97),
                Field("packetization", &Codec::packetization, "bar")),
          AllOf(Field("id", &Codec::id, 99),
                Field("packetization", &Codec::packetization, std::nullopt))));
}

TEST_F(VideoChannelSingleThreadTest,
       ConsidersAllCodecsWithDiffrentPacketizationsInRemoteAnswer) {
  Codec vp8 = CreateVideoCodec(96, "VP8");
  Codec vp8_raw = CreateVideoCodec(97, "VP8");
  vp8_raw.packetization = kPacketizationParamRaw;
  VideoContentDescription local;
  local.set_codecs({vp8, vp8_raw});
  VideoContentDescription remote;
  remote.set_codecs({vp8_raw, vp8});

  CreateChannels(0, 0);
  ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kOffer).ok());
  ASSERT_TRUE(channel1_->SetRemoteContent(&remote, SdpType::kAnswer).ok());

  EXPECT_THAT(media_receive_channel1_impl()->recv_codecs(),
              UnorderedElementsAre(
                  AllOf(Field(&Codec::id, 96),
                        Field(&Codec::packetization, std::nullopt)),
                  AllOf(Field(&Codec::id, 97),
                        Field(&Codec::packetization, kPacketizationParamRaw))));
  EXPECT_THAT(media_send_channel1_impl()->send_codecs(),
              UnorderedElementsAre(
                  AllOf(Field(&Codec::id, 97),
                        Field(&Codec::packetization, kPacketizationParamRaw)),
                  AllOf(Field(&Codec::id, 96),
                        Field(&Codec::packetization, std::nullopt))));
}

TEST_F(VideoChannelSingleThreadTest,
       ConsidersAllCodecsWithDiffrentPacketizationsInLocalAnswer) {
  Codec vp8 = CreateVideoCodec(96, "VP8");
  Codec vp8_raw = CreateVideoCodec(97, "VP8");
  vp8_raw.packetization = kPacketizationParamRaw;
  VideoContentDescription local;
  local.set_codecs({vp8_raw, vp8});
  VideoContentDescription remote;
  remote.set_codecs({vp8, vp8_raw});

  CreateChannels(0, 0);
  ASSERT_TRUE(channel1_->SetRemoteContent(&remote, SdpType::kOffer).ok());
  ASSERT_TRUE(channel1_->SetLocalContent(&local, SdpType::kAnswer).ok());

  EXPECT_THAT(
      media_receive_channel1_impl()->recv_codecs(),
      ElementsAre(AllOf(Field(&Codec::id, 97),
                        Field(&Codec::packetization, kPacketizationParamRaw)),
                  AllOf(Field(&Codec::id, 96),
                        Field(&Codec::packetization, std::nullopt))));
  EXPECT_THAT(
      media_send_channel1_impl()->send_codecs(),
      ElementsAre(AllOf(Field(&Codec::id, 96),
                        Field(&Codec::packetization, std::nullopt)),
                  AllOf(Field(&Codec::id, 97),
                        Field(&Codec::packetization, kPacketizationParamRaw))));
}

TEST_F(VideoChannelSingleThreadTest,
       TestSetRtpTransportNullClearsReceiveSinks) {
  Base::TestSetRtpTransportNullClearsReceiveSinks();
}

TEST_F(VideoChannelSingleThreadTest,
       TestRegisterRtpDemuxerSinkPropagatesReceiveSsrcs) {
  Base::TestRegisterRtpDemuxerSinkPropagatesReceiveSsrcs();
}

TEST_F(VideoChannelSingleThreadTest,
       TestUpdateRemoteStreamsClearsRemovedSinks) {
  Base::TestUpdateRemoteStreamsClearsRemovedSinks();
}

TEST_F(VideoChannelSingleThreadTest,
       TestUpdateRemoteStreamsClearsUnsignaledSinks) {
  Base::TestUpdateRemoteStreamsClearsUnsignaledSinks();
}

// VideoChannelDoubleThreadTest
TEST_F(VideoChannelDoubleThreadTest, TestInit) {
  Base::TestInit();
}

TEST_F(VideoChannelDoubleThreadTest, TestDeinit) {
  Base::TestDeinit();
}

TEST_F(VideoChannelDoubleThreadTest, TestSetContents) {
  Base::TestSetContents();
}

TEST_F(VideoChannelDoubleThreadTest, TestSetContentsExtmapAllowMixedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/true);
}

TEST_F(VideoChannelDoubleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCaller) {
  Base::TestSetContentsExtmapAllowMixedCaller(/*offer=*/true, /*answer=*/false);
}

TEST_F(VideoChannelDoubleThreadTest, TestSetContentsExtmapAllowMixedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/true);
}

TEST_F(VideoChannelDoubleThreadTest,
       TestSetContentsExtmapAllowMixedNotSupportedAsCallee) {
  Base::TestSetContentsExtmapAllowMixedCallee(/*offer=*/true, /*answer=*/false);
}

TEST_F(VideoChannelDoubleThreadTest, TestSetContentsNullOffer) {
  Base::TestSetContentsNullOffer();
}

TEST_F(VideoChannelDoubleThreadTest, TestSetContentsRtcpMux) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VideoChannelDoubleThreadTest, TestSetContentsRtcpMuxWithPrAnswer) {
  Base::TestSetContentsRtcpMux();
}

TEST_F(VideoChannelDoubleThreadTest, TestChangeStreamParamsInContent) {
  Base::TestChangeStreamParamsInContent();
}

TEST_F(VideoChannelDoubleThreadTest, TestPlayoutAndSendingStates) {
  Base::TestPlayoutAndSendingStates();
}

TEST_F(VideoChannelDoubleThreadTest, TestMediaContentDirection) {
  Base::TestMediaContentDirection();
}

TEST_F(VideoChannelDoubleThreadTest, TestCallSetup) {
  Base::TestCallSetup();
}

TEST_F(VideoChannelDoubleThreadTest, SendRtpToRtp) {
  Base::SendRtpToRtp();
}

TEST_F(VideoChannelDoubleThreadTest, SendDtlsSrtpToDtlsSrtp) {
  Base::SendDtlsSrtpToDtlsSrtp(0, 0);
}

TEST_F(VideoChannelDoubleThreadTest, SendDtlsSrtpToDtlsSrtpRtcpMux) {
  Base::SendDtlsSrtpToDtlsSrtp(RTCP_MUX, RTCP_MUX);
}

TEST_F(VideoChannelDoubleThreadTest, SendEarlyMediaUsingRtcpMuxSrtp) {
  Base::SendEarlyMediaUsingRtcpMuxSrtp();
}

TEST_F(VideoChannelDoubleThreadTest, SendRtpToRtpOnThread) {
  Base::SendRtpToRtpOnThread();
}

TEST_F(VideoChannelDoubleThreadTest, SendWithWritabilityLoss) {
  Base::SendWithWritabilityLoss();
}

TEST_F(VideoChannelDoubleThreadTest, TestSetContentFailure) {
  Base::TestSetContentFailure();
}

TEST_F(VideoChannelDoubleThreadTest, TestSendTwoOffers) {
  Base::TestSendTwoOffers();
}

TEST_F(VideoChannelDoubleThreadTest, TestUpdateRemoteStreamsRaceWithRtpPacket) {
  Base::TestUpdateRemoteStreamsRaceWithRtpPacket();
}

TEST_F(VideoChannelDoubleThreadTest, TestReceiveTwoOffers) {
  Base::TestReceiveTwoOffers();
}

TEST_F(VideoChannelDoubleThreadTest, TestSendPrAnswer) {
  Base::TestSendPrAnswer();
}

TEST_F(VideoChannelDoubleThreadTest, TestReceivePrAnswer) {
  Base::TestReceivePrAnswer();
}

TEST_F(VideoChannelDoubleThreadTest, SendBundleToBundle) {
  Base::SendBundleToBundle(kVideoPts, false, false);
}

TEST_F(VideoChannelDoubleThreadTest, SendBundleToBundleSecure) {
  Base::SendBundleToBundle(kVideoPts, false, true);
}

TEST_F(VideoChannelDoubleThreadTest, SendBundleToBundleWithRtcpMux) {
  Base::SendBundleToBundle(kVideoPts, true, false);
}

TEST_F(VideoChannelDoubleThreadTest, SendBundleToBundleWithRtcpMuxSecure) {
  Base::SendBundleToBundle(kVideoPts, true, true);
}

TEST_F(VideoChannelDoubleThreadTest, TestOnTransportReadyToSend) {
  Base::TestOnTransportReadyToSend();
}

TEST_F(VideoChannelDoubleThreadTest, DefaultMaxBitrateIsUnlimited) {
  Base::DefaultMaxBitrateIsUnlimited();
}

TEST_F(VideoChannelDoubleThreadTest, SocketOptionsMergedOnSetTransport) {
  Base::SocketOptionsMergedOnSetTransport();
}

}  // namespace

}  // namespace webrtc
