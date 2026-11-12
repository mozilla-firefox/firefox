/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "MediaPipeline.h"

#include "AudioSegment.h"
#include "Canonicals.h"
#include "ImageContainer.h"
#include "MediaConduitInterface.h"
#include "MediaPipelineFilter.h"
#include "MediaTrackGraph.h"
#include "MediaTrackListener.h"
#include "MediaTransportHandler.h"
#include "MockConduit.h"
#include "PeerConnectionCtx.h"
#include "SharedBuffer.h"
#include "VideoSegment.h"
#include "WebrtcCallWrapper.h"
#include "WebrtcEnvironmentWrapper.h"
#include "WebrtcTaskQueueWrapper.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "logging.h"
#include "modules/audio_device/include/fake_audio_device.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mtransport_test_utils.h"
#include "nss.h"
#include "ssl.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"

using namespace mozilla;
MOZ_MTLOG_MODULE("transportbridge")

static MtransportTestUtils* test_utils;

namespace {
class MainAsCurrent : public webrtc::TaskQueueBase {
 public:
  MainAsCurrent()
      : mTaskQueue(CreateWebrtcTaskQueueWrapper(
            do_AddRef(GetMainThreadSerialEventTarget()), "MainAsCurrent"_ns,
            TailDispatchPolicy::NoTailDispatch)),
        mWebrtcTaskQueue(([&] {
          // Shady but fine, as this raw pointer points to the WebrtcTaskQueue
          // owned and kept alive by mTaskQueue.
          webrtc::TaskQueueBase* queue{};
          MOZ_ALWAYS_SUCCEEDS(mTaskQueue->Dispatch(NS_NewRunnableFunction(
              "MainAsCurrent::Current",
              [&] { queue = webrtc::TaskQueueBase::Current(); })));
          NS_ProcessPendingEvents(nullptr);
          MOZ_RELEASE_ASSERT(queue);
          return queue;
        })()),
        mSetter(mWebrtcTaskQueue) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
  }

  ~MainAsCurrent() = default;

  void Delete() override { delete this; }

  void PostTaskImpl(absl::AnyInvocable<void() &&> task,
                    const PostTaskTraits& /*traits*/,
                    const webrtc::Location& location) override {
    mWebrtcTaskQueue->PostTask(std::move(task), location);
  }

  void PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task,
                           webrtc::TimeDelta delay,
                           const PostDelayedTaskTraits& traits,
                           const webrtc::Location& location) override {
    if (traits.high_precision) {
      mWebrtcTaskQueue->PostDelayedHighPrecisionTask(std::move(task), delay,
                                                     location);
      return;
    }
    mWebrtcTaskQueue->PostDelayedTask(std::move(task), delay, location);
  }

 private:
  RefPtr<TaskQueue> mTaskQueue;
  webrtc::TaskQueueBase* mWebrtcTaskQueue;
  CurrentTaskQueueSetter mSetter;
};

class FakeAudioTrack : public ProcessedMediaTrack {
 public:
  FakeAudioTrack()
      : ProcessedMediaTrack(44100, MediaSegment::AUDIO, nullptr),
        mMutex("Fake AudioTrack") {
    NS_NewTimerWithFuncCallback(getter_AddRefs(mTimer),
                                FakeAudioTrackGenerateData, this, 20,
                                nsITimer::TYPE_REPEATING_SLACK,
                                "FakeAudioTrack::FakeAudioTrackGenerateData"_ns,
                                test_utils->sts_target());
  }

  void Destroy() override {
    MutexAutoLock lock(mMutex);
    MOZ_RELEASE_ASSERT(!mDestroyed);
    mDestroyed = true;
    mTimer->Cancel();
    mTimer = nullptr;
  }

  void QueueSetAutoend(bool) override {}

  void AddListener(MediaTrackListener* aListener) override {
    MutexAutoLock lock(mMutex);
    MOZ_RELEASE_ASSERT(!mListener);
    mListener = aListener;
  }

  RefPtr<GenericPromise> RemoveListener(
      MediaTrackListener* aListener) override {
    MutexAutoLock lock(mMutex);
    MOZ_RELEASE_ASSERT(mListener == aListener);
    mListener = nullptr;
    return GenericPromise::CreateAndResolve(true, __func__);
  }

  void ProcessInput(GraphTime aFrom, GraphTime aTo, uint32_t aFlags) override {}

  uint32_t NumberOfChannels() const override { return NUM_CHANNELS; }

 private:
  Mutex mMutex MOZ_UNANNOTATED;
  MediaTrackListener* mListener = nullptr;
  nsCOMPtr<nsITimer> mTimer;
  int mCount = 0;

  static const int AUDIO_BUFFER_SIZE = 1600;
  static const int NUM_CHANNELS = 2;
  static void FakeAudioTrackGenerateData(nsITimer* /*timer*/, void* closure) {
    auto t = static_cast<FakeAudioTrack*>(closure);
    MutexAutoLock lock(t->mMutex);
    if (t->mDestroyed) {
      return;
    }
    CheckedInt<size_t> bufferSize(sizeof(int16_t));
    bufferSize *= NUM_CHANNELS;
    bufferSize *= AUDIO_BUFFER_SIZE;
    RefPtr<SharedBuffer> samples = SharedBuffer::Create(bufferSize);
    int16_t* data = reinterpret_cast<int16_t*>(samples->Data());
    for (int i = 0; i < (AUDIO_BUFFER_SIZE * NUM_CHANNELS); i++) {
      // saw tooth audio sample
      data[i] = ((t->mCount % 8) * 4000) - (7 * 4000) / 2;
      t->mCount++;
    }

    AudioSegment segment;
    AutoTArray<const int16_t*, 1> channels;
    channels.AppendElement(data);
    segment.AppendFrames(samples.forget(), channels, AUDIO_BUFFER_SIZE,
                         PRINCIPAL_HANDLE_NONE);

    if (t->mListener) {
      t->mListener->NotifyQueuedChanges(nullptr, 0, segment);
    }
  }
};

template <typename Function>
void RunOnSts(Function&& aFunction) {
  MOZ_ALWAYS_SUCCEEDS(test_utils->SyncDispatchToSTS(
      NS_NewRunnableFunction(__func__, [&] { aFunction(); })));
}

class LoopbackTransport : public MediaTransportHandler {
 public:
  LoopbackTransport() : MediaTransportHandler() {
    RunOnSts([&] {
      SetState("mux", TransportLayer::TS_INIT);
      SetRtcpState("mux", TransportLayer::TS_INIT);
      SetState("non-mux", TransportLayer::TS_INIT);
      SetRtcpState("non-mux", TransportLayer::TS_INIT);
    });
  }

  static void InitAndConnect(LoopbackTransport& client,
                             LoopbackTransport& server) {
    client.Connect(&server);
    server.Connect(&client);
  }

  void Connect(LoopbackTransport* peer) { peer_ = peer; }

  void Shutdown() { peer_ = nullptr; }

  RefPtr<IceLogPromise> GetIceLog(const nsCString& /*aPattern*/) override {
    return nullptr;
  }

  void ClearIceLog() override {}
  void EnterPrivateMode() override {}
  void ExitPrivateMode() override {}

  void CreateIceCtx(const std::string& aName) override {}

  nsresult SetIceConfig(const nsTArray<dom::RTCIceServer>& /*aIceServers*/,
                        dom::RTCIceTransportPolicy /*aIcePolicy*/) override {
    return NS_OK;
  }

  // We will probably be able to move the proxy lookup stuff into
  // this class once we move mtransport to its own process.
  void SetProxyConfig(NrSocketProxyConfig&& aProxyConfig) override {}

  void EnsureProvisionalTransport(const std::string& aTransportId,
                                  const std::string& aLocalUfrag,
                                  const std::string& aLocalPwd,
                                  int aComponentCount) override {}

  void SetTargetForDefaultLocalAddressLookup(const std::string& aTargetIp,
                                             uint16_t aTargetPort) override {}

  // We set default-route-only as late as possible because it depends on what
  // capture permissions have been granted on the window, which could easily
  // change between Init (ie; when the PC is created) and StartIceGathering
  // (ie; when we set the local description).
  void StartIceGathering(bool aDefaultRouteOnly, bool aObfuscateAddresses,
                         // TODO: It probably makes sense to look
                         // this up internally
                         const nsTArray<NrIceStunAddr>& aStunAddrs) override {}

  void SetCandidatePoolSize(
      uint8_t aSize, bool aDefaultRouteOnly, bool aObfuscateAddresses,
      const nsTArray<NrIceStunAddr>& aStunAddrs) override {}

  void EmptyCandidatePool() override {}

  void ActivateTransport(
      const std::string& aTransportId, const std::string& aLocalUfrag,
      const std::string& aLocalPwd, size_t aComponentCount,
      const std::string& aUfrag, const std::string& aPassword,
      const nsTArray<uint8_t>& aKeyDer, const nsTArray<uint8_t>& aCertDer,
      SSLKEAType aAuthType, bool aDtlsClient, const DtlsDigestList& aDigests,
      bool aPrivacyRequested) override {}

  void RemoveTransportsExcept(
      const std::set<std::string>& aTransportIds) override {}

  void StartIceChecks(bool aIsControlling,
                      const std::vector<std::string>& aIceOptions) override {}

  void AddIceCandidate(const std::string& aTransportId,
                       const std::string& aCandidate, const std::string& aUfrag,
                       const std::string& aObfuscatedAddress) override {}

  void UpdateNetworkState(bool aOnline) override {}

  RefPtr<dom::RTCStatsPromise> GetIceStats(
      const std::string& /*aTransportId*/,
      DOMHighResTimeStamp /*aNow*/) override {
    return nullptr;
  }

  void SendPacket(const std::string& aTransportId,
                  MediaPacket&& aPacket) override {
    peer_->LoopbackPacketReceived(aTransportId, aPacket);
  }

  void SetState(const std::string& aTransportId, TransportLayer::State aState) {
    MediaTransportHandler::OnStateChange(aTransportId, aState, {});
  }

  void SetRtcpState(const std::string& aTransportId,
                    TransportLayer::State aState) {
    MediaTransportHandler::OnRtcpStateChange(aTransportId, aState);
  }

  void LoopbackPacketReceived(const std::string& aTransportId,
                              const MediaPacket& aPacket) {
    if (aPacket.len() && aPacket.type() == MediaPacket::RTCP) {
      ++rtcp_packets_received_;
    }
    mEvents->mRtpPacketReceived.Notify(aTransportId, aPacket);
  }

  int RtcpPacketsReceived() const { return rtcp_packets_received_; }

 private:
  RefPtr<LoopbackTransport> peer_;
  std::atomic<int> rtcp_packets_received_{0};
};

struct MediaPipelineTestOptions {
  bool is_rtcp_mux = true;
  bool activate_receive = true;
  unsigned int ms_until_filter_update = 500;
  unsigned int ms_of_traffic_after_answer = 10000;
};

class TestAgent {
 public:
  explicit TestAgent(const RefPtr<WebrtcEnvironmentWrapper>& aEnvWrapper,
                     const RefPtr<SharedWebrtcState>& aSharedState)
      : control_(aSharedState->mCallWorkerThread),
        audio_config_(109, "opus", 48000, 2, false),
        call_(WebrtcCallWrapper::Create(
            aEnvWrapper, mozilla::dom::RTCStatsTimestampMaker::Create(),
            nullptr, aSharedState)),
        audio_conduit_(
            AudioSessionConduit::Create(call_, test_utils->sts_target())),
        transport_(new LoopbackTransport) {
    (void)WaitFor(InvokeAsync(call_->mCallThread, __func__, [&] {
      audio_conduit_->InitControl(&control_);
      return GenericPromise::CreateAndResolve(true, "TestAgent()");
    }));
  }

  static void Connect(TestAgent* client, TestAgent* server) {
    LoopbackTransport::InitAndConnect(*client->transport_, *server->transport_);
  }

  virtual void CreatePipeline(const std::string& aTransportId,
                              const MediaPipelineTestOptions& aOptions) = 0;

  void SetState_s(const std::string& aTransportId,
                  TransportLayer::State aState) {
    transport_->SetState(aTransportId, aState);
  }

  void SetRtcpState_s(const std::string& aTransportId,
                      TransportLayer::State aState) {
    transport_->SetRtcpState(aTransportId, aState);
  }

  void UpdateTransport_s(const std::string& aTransportId,
                         UniquePtr<MediaPipelineFilter>&& aFilter) {
    audio_pipeline_->UpdateTransport_s(aTransportId, std::move(aFilter), false);
  }

  void Stop() {
    MOZ_MTLOG(ML_DEBUG, "Stopping");

    control_.Update([](auto& aControl) {
      aControl.mTransmitting = false;
      aControl.mReceiving = false;
    });
  }

  void Shutdown_s() { transport_->Shutdown(); }

  void Shutdown() {
    if (audio_pipeline_) {
      audio_pipeline_->Shutdown();
    }
    if (audio_conduit_) {
      (void)WaitFor(audio_conduit_->Shutdown());
    }
    if (call_) {
      call_->Destroy();
    }
    if (audio_track_) {
      audio_track_->Destroy();
      audio_track_ = nullptr;
    }

    test_utils->SyncDispatchToSTS(WrapRunnable(this, &TestAgent::Shutdown_s));
  }

  uint32_t GetRemoteSSRC() {
    return audio_conduit_->GetRemoteSSRC().valueOr(0);
  }

  uint32_t GetLocalSSRC() {
    std::vector<uint32_t> res;
    res = audio_conduit_->GetLocalSSRCs();
    return res.empty() ? 0 : res[0];
  }

  int GetAudioRtpCountSent() { return audio_pipeline_->RtpPacketsSent(); }

  int GetAudioRtpCountReceived() {
    return audio_pipeline_->RtpPacketsReceived();
  }

  int GetAudioRtcpCountSent() { return audio_pipeline_->RtcpPacketsSent(); }

  int GetAudioRtcpCountReceived() { return transport_->RtcpPacketsReceived(); }

 protected:
  ConcreteControl control_;
  AudioCodecConfig audio_config_;
  RefPtr<WebrtcCallWrapper> call_;
  RefPtr<AudioSessionConduit> audio_conduit_;
  RefPtr<FakeAudioTrack> audio_track_;
  // TODO(bcampen@mozilla.com): Right now this does not let us test RTCP in
  // both directions; only the sender's RTCP is sent, but the receiver should
  // be sending it too.
  RefPtr<MediaPipeline> audio_pipeline_;
  RefPtr<LoopbackTransport> transport_;
};

class TestAgentSend : public TestAgent {
 public:
  explicit TestAgentSend(const RefPtr<WebrtcEnvironmentWrapper>& aEnvWrapper,
                         const RefPtr<SharedWebrtcState>& aSharedState)
      : TestAgent(aEnvWrapper, aSharedState) {
    control_.Update([&](auto& aControl) {
      aControl.mAudioSendCodec = Some(audio_config_);
    });
    audio_track_ = new FakeAudioTrack();
  }

  virtual void CreatePipeline(const std::string& aTransportId,
                              const MediaPipelineTestOptions& /*aOptions*/) {
    std::string test_pc;

    RefPtr<MediaPipelineTransmit> audio_pipeline =
        MediaPipelineTransmit::Create(
            test_pc, transport_, AbstractThread::MainThread(),
            test_utils->sts_target(), false, audio_conduit_);
    (void)WaitFor(InvokeAsync(call_->mCallThread, __func__, [&] {
      audio_pipeline->InitControl(&control_);
      return GenericPromise::CreateAndResolve(true, __func__);
    }));

    audio_pipeline->SetSendTrackOverride(audio_track_);
    control_.Update([](auto& aControl) { aControl.mTransmitting = true; });
    audio_pipeline->UpdateTransport_m(aTransportId, nullptr, true);
    audio_pipeline_ = audio_pipeline;
  }
};

class TestAgentReceive : public TestAgent {
 public:
  explicit TestAgentReceive(const RefPtr<WebrtcEnvironmentWrapper>& aEnvWrapper,
                            const RefPtr<SharedWebrtcState>& aSharedState)
      : TestAgent(aEnvWrapper, aSharedState) {
    control_.Update([&](auto& aControl) {
      std::vector<AudioCodecConfig> codecs;
      codecs.push_back(audio_config_);
      aControl.mAudioRecvCodecs = codecs;
    });
  }

  virtual void CreatePipeline(const std::string& aTransportId,
                              const MediaPipelineTestOptions& aOptions) {
    std::string test_pc;

    auto audio_pipeline = MakeRefPtr<MediaPipelineReceiveAudio>(
        test_pc, transport_, AbstractThread::MainThread(),
        test_utils->sts_target(),
        static_cast<AudioSessionConduit*>(audio_conduit_.get()), nullptr,
        TrackingId(), PRINCIPAL_HANDLE_NONE, PrincipalPrivacy::NonPrivate);
    (void)WaitFor(InvokeAsync(call_->mCallThread, __func__, [&] {
      audio_pipeline->InitControl(&control_);
      return GenericPromise::CreateAndResolve(true, __func__);
    }));

    control_.Update([activate = aOptions.activate_receive](auto& aControl) {
      aControl.mReceiving = activate;
    });
    audio_pipeline->UpdateTransport_m(aTransportId, std::move(bundle_filter_),
                                      true);
    audio_pipeline_ = audio_pipeline;
  }

  void SetBundleFilter(UniquePtr<MediaPipelineFilter>&& filter) {
    bundle_filter_ = std::move(filter);
  }

  void UpdateTransport_s(const std::string& aTransportId,
                         UniquePtr<MediaPipelineFilter>&& filter) {
    audio_pipeline_->UpdateTransport_s(aTransportId, std::move(filter), false);
  }

 private:
  UniquePtr<MediaPipelineFilter> bundle_filter_;
};

void WaitFor(TimeDuration aDuration) {
  bool done = false;
  NS_DelayedDispatchToCurrentThread(
      NS_NewRunnableFunction(__func__, [&] { done = true; }),
      aDuration.ToMilliseconds());
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "WaitFor(TimeDuration aDuration)"_ns, [&] { return done; });
}

webrtc::AudioState::Config CreateAudioStateConfig(
    const webrtc::Environment& aEnv) {
  webrtc::AudioState::Config audio_state_config;
  audio_state_config.audio_mixer = webrtc::AudioMixerImpl::Create();

  webrtc::BuiltinAudioProcessingBuilder audio_processing_builder;
  audio_state_config.audio_processing = audio_processing_builder.Build(aEnv);
  audio_state_config.audio_device_module = new webrtc::FakeAudioDeviceModule();
  return audio_state_config;
}

class MediaPipelineTest : public ::testing::Test {
 public:
  explicit MediaPipelineTest(MediaPipelineTestOptions options = {})
      : main_task_queue_(
            std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter>(
                new MainAsCurrent())),
        options_(options),
        env_wrapper_(WebrtcEnvironmentWrapper::Create(
            mozilla::dom::RTCStatsTimestampMaker::Create())),
        shared_state_(MakeAndAddRef<SharedWebrtcState>(
            AbstractThread::MainThread(),
            CreateAudioStateConfig(env_wrapper_->Environment()),
            already_AddRefed(
                webrtc::CreateBuiltinAudioDecoderFactory().release()),
            WrapUnique(new webrtc::MozTrialsConfig()))),
        p1_(env_wrapper_, shared_state_),
        p2_(env_wrapper_, shared_state_) {}

  ~MediaPipelineTest() {
    p1_.Shutdown();
    p2_.Shutdown();
  }

  static void SetUpTestCase() {
    test_utils = new MtransportTestUtils();
    NSS_NoDB_Init(nullptr);
    NSS_SetDomesticPolicy();
  }

  // Setup transport.
  void InitTransports() {
    test_utils->SyncDispatchToSTS(
        WrapRunnableNM(&TestAgent::Connect, &p2_, &p1_));
  }

  // Verify RTP and RTCP
  void TestAudioSend(MediaPipelineTestOptions options,
                     UniquePtr<MediaPipelineFilter>&& initialFilter = nullptr,
                     UniquePtr<MediaPipelineFilter>&& refinedFilter = nullptr) {
    bool bundle = !!(initialFilter);
    // We do not support testing bundle without rtcp mux, since that doesn't
    // make any sense.
    ASSERT_FALSE(!options.is_rtcp_mux && bundle);

    p2_.SetBundleFilter(std::move(initialFilter));

    // Setup transport flows
    InitTransports();

    std::string transportId = options.is_rtcp_mux ? "mux" : "non-mux";
    p1_.CreatePipeline(transportId, options);
    p2_.CreatePipeline(transportId, options);

    // Set state of transports to CONNECTING. MediaPipeline doesn't really care
    // about this transition, but we're trying to simluate what happens in a
    // real case.
    RunOnSts([&] {
      p1_.SetState_s(transportId, TransportLayer::TS_CONNECTING);
      p1_.SetRtcpState_s(transportId, TransportLayer::TS_CONNECTING);
      p2_.SetState_s(transportId, TransportLayer::TS_CONNECTING);
      p2_.SetRtcpState_s(transportId, TransportLayer::TS_CONNECTING);
    });

    WaitFor(TimeDuration::FromMilliseconds(10));

    // Set state of transports to OPEN (ie; connected). This should result in
    // media flowing.
    RunOnSts([&] {
      p1_.SetState_s(transportId, TransportLayer::TS_OPEN);
      p1_.SetRtcpState_s(transportId, TransportLayer::TS_OPEN);
      p2_.SetState_s(transportId, TransportLayer::TS_OPEN);
      p2_.SetRtcpState_s(transportId, TransportLayer::TS_OPEN);
    });

    if (bundle) {
      WaitFor(TimeDuration::FromMilliseconds(options.ms_until_filter_update));

      // Leaving refinedFilter not set implies we want to just update with
      // the other side's SSRC
      if (!refinedFilter) {
        refinedFilter = std::make_unique<MediaPipelineFilter>();
        // Might not be safe, strictly speaking.
        refinedFilter->AddRemoteSSRC(p1_.GetLocalSSRC());
      }

      RunOnSts([&] {
        p2_.UpdateTransport_s(transportId, std::move(refinedFilter));
      });
    }

    // wait for some RTP/RTCP tx and rx to happen
    WaitFor(TimeDuration::FromMilliseconds(options.ms_of_traffic_after_answer));

    p1_.Stop();
    p2_.Stop();

    // wait for any packets in flight to arrive
    WaitFor(TimeDuration::FromMilliseconds(200));

    p1_.Shutdown();
    p2_.Shutdown();

    if (!bundle) {
      // If we are filtering, allow the test-case to do this checking.
      ASSERT_GE(p1_.GetAudioRtpCountSent(), 40);
      ASSERT_EQ(p1_.GetAudioRtpCountReceived(), p2_.GetAudioRtpCountSent());
      ASSERT_EQ(p1_.GetAudioRtpCountSent(), p2_.GetAudioRtpCountReceived());
    }

    // No RTCP packets should have been dropped, because we do not filter them.
    // Calling ShutdownMedia_m on both pipelines does not stop the flow of
    // RTCP. So, we might be off by one here.
    ASSERT_LE(p2_.GetAudioRtcpCountReceived(), p1_.GetAudioRtcpCountSent());
    ASSERT_GE(p2_.GetAudioRtcpCountReceived() + 1, p1_.GetAudioRtcpCountSent());
  }

  void TestAudioReceiverBundle(
      UniquePtr<MediaPipelineFilter>&& initialFilter,
      UniquePtr<MediaPipelineFilter>&& refinedFilter = nullptr,
      MediaPipelineTestOptions options = {}) {
    TestAudioSend(options, std::move(initialFilter), std::move(refinedFilter));
  }

 protected:
  // main_task_queue_ has this type to make sure it goes through Delete() when
  // we're destroyed.
  std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter>
      main_task_queue_;
  const MediaPipelineTestOptions options_;
  const RefPtr<WebrtcEnvironmentWrapper> env_wrapper_;
  const RefPtr<SharedWebrtcState> shared_state_;
  TestAgentSend p1_;
  TestAgentReceive p2_;
};

class MediaPipelineFilterTest : public ::testing::Test {
 public:
  bool Filter(MediaPipelineFilter& filter, uint32_t ssrc, uint8_t payload_type,
              const Maybe<std::string>& mid = Nothing()) {
    webrtc::RTPHeader header;
    header.ssrc = ssrc;
    header.payloadType = payload_type;
    mid.apply([&](const auto& mid) { header.extension.mid = mid; });
    return filter.Filter(header);
  }
};

TEST_F(MediaPipelineFilterTest, TestConstruct) { MediaPipelineFilter filter; }

TEST_F(MediaPipelineFilterTest, TestDefault) {
  MediaPipelineFilter filter;
  EXPECT_FALSE(Filter(filter, 233, 110));
}

TEST_F(MediaPipelineFilterTest, TestSSRCFilter) {
  MediaPipelineFilter filter;
  filter.AddRemoteSSRC(555);
  EXPECT_TRUE(Filter(filter, 555, 110));
  EXPECT_FALSE(Filter(filter, 556, 110));
}

TEST_F(MediaPipelineFilterTest, TestSSRCFilterOverridesPayloadTypeFilter) {
  MediaPipelineFilter filter;
  filter.AddRemoteSSRC(555);
  filter.AddUniqueReceivePT(110);
  // We have a configured ssrc but still need to allow payload type matching.
  EXPECT_TRUE(Filter(filter, 556, 110));
  EXPECT_TRUE(Filter(filter, 555, 110));
}

#define SSRC(ssrc)                                                    \
  ((ssrc >> 24) & 0xFF), ((ssrc >> 16) & 0xFF), ((ssrc >> 8) & 0xFF), \
      (ssrc & 0xFF)
#define REPORT_FRAGMENT(ssrc) \
  SSRC(ssrc), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

#define RTCP_TYPEINFO(num_rrs, type, size) 0x80 + num_rrs, type, 0, size

TEST_F(MediaPipelineFilterTest, TestMidFilter) {
  MediaPipelineFilter filter;
  const auto mid = Some(std::string("mid0"));
  filter.SetRemoteMediaStreamId(mid);

  EXPECT_FALSE(Filter(filter, 16, 110));
  EXPECT_TRUE(Filter(filter, 16, 110, mid));
  EXPECT_TRUE(Filter(filter, 16, 110));
  EXPECT_FALSE(Filter(filter, 17, 110));

  // The mid filter maintains a set of SSRCs. Adding a new SSRC should work
  // and still allow previous SSRCs to work. Unrecognized SSRCs should still be
  // filtered out.
  EXPECT_TRUE(Filter(filter, 18, 111, mid));
  EXPECT_TRUE(Filter(filter, 18, 111));
  EXPECT_TRUE(Filter(filter, 16, 110));
  EXPECT_FALSE(Filter(filter, 17, 110));
}

TEST_F(MediaPipelineFilterTest, TestPayloadTypeFilter) {
  MediaPipelineFilter filter;
  filter.AddUniqueReceivePT(110);
  EXPECT_TRUE(Filter(filter, 555, 110));
  EXPECT_FALSE(Filter(filter, 556, 111));
  // Matching based on unique payload type causes us to learn the ssrc.
  EXPECT_TRUE(Filter(filter, 555, 98));
  // Once we have learned an SSRC we still need to learn new ones
  // based on payload type.
  EXPECT_TRUE(Filter(filter, 557, 110));
}

TEST_F(MediaPipelineFilterTest, TestSSRCMovedWithMid) {
  MediaPipelineFilter filter;
  const auto mid0 = Some(std::string("mid0"));
  const auto mid1 = Some(std::string("mid1"));
  filter.SetRemoteMediaStreamId(mid0);
  ASSERT_TRUE(Filter(filter, 555, 110, mid0));
  ASSERT_TRUE(Filter(filter, 555, 110));
  // Present a new MID binding
  ASSERT_FALSE(Filter(filter, 555, 110, mid1));
  ASSERT_FALSE(Filter(filter, 555, 110));
}

TEST_F(MediaPipelineFilterTest, TestRemoteSDPNoSSRCs) {
  // If the remote SDP doesn't have SSRCs, right now this is a no-op and
  // there is no point of even incorporating a filter, but we make the
  // behavior consistent to avoid confusion.
  MediaPipelineFilter filter;
  const auto mid = Some(std::string("mid0"));
  filter.SetRemoteMediaStreamId(mid);
  filter.AddUniqueReceivePT(111);
  EXPECT_TRUE(Filter(filter, 555, 110, mid));
  EXPECT_TRUE(Filter(filter, 555, 110));

  // Update but remember binding./
  MediaPipelineFilter filter2;

  filter.Update(filter2, true);

  // Ensure that the old SSRC still works.
  EXPECT_TRUE(Filter(filter, 555, 110));

  // Forget the previous binding
  MediaPipelineFilter filter3;
  filter3.SetRemoteMediaStreamId(Some(std::string("mid1")));
  filter.Update(filter3, true);

  ASSERT_FALSE(Filter(filter, 555, 110));
}

TEST_F(MediaPipelineTest, TestAudioSendNoMux) {
  TestAudioSend({.is_rtcp_mux = false});
}

TEST_F(MediaPipelineTest, TestAudioSendMux) {
  TestAudioSend({.is_rtcp_mux = true});
}

TEST_F(MediaPipelineTest, TestAudioSendBundle) {
  auto filter = std::make_unique<MediaPipelineFilter>();
  // These durations have to be _extremely_ long to have any assurance that
  // some RTCP will be sent at all. This is because the first RTCP packet
  // is sometimes sent before the transports are ready, which causes it to
  // be dropped.
  TestAudioReceiverBundle(
      std::move(filter),
      // We do not specify the filter for the remote description, so it will be
      // set to something sane after a short time.
      nullptr,
      {.ms_until_filter_update = 10000, .ms_of_traffic_after_answer = 10000});

  // Some packets should have been dropped, but not all
  ASSERT_GT(p1_.GetAudioRtpCountSent(), p2_.GetAudioRtpCountReceived());
  ASSERT_GT(p2_.GetAudioRtpCountReceived(), 40);
  ASSERT_GT(p1_.GetAudioRtcpCountSent(), 1);
}

TEST_F(MediaPipelineTest, TestAudioSendEmptyBundleFilter) {
  auto filter = std::make_unique<MediaPipelineFilter>();
  auto bad_answer_filter = std::make_unique<MediaPipelineFilter>();
  TestAudioReceiverBundle(std::move(filter), std::move(bad_answer_filter));
  // Filter is empty, so should drop everything.
  ASSERT_EQ(0, p2_.GetAudioRtpCountReceived());
}

TEST_F(MediaPipelineTest, TestAudioInactiveNoRecv) {
  auto filter = std::make_unique<MediaPipelineFilter>();
  TestAudioReceiverBundle(std::move(filter), nullptr,
                          {.activate_receive = false,
                           .ms_until_filter_update = 200,
                           .ms_of_traffic_after_answer = 800});

  // Packets should have been sent but not received.
  ASSERT_NE(p1_.GetAudioRtpCountSent(), 0);
  ASSERT_EQ(p2_.GetAudioRtpCountReceived(), 0);
}

// A video SourceMediaTrack outside any graph, whose appended data the test
// takes.
class MockSourceMediaTrack : public SourceMediaTrack {
 public:
  MockSourceMediaTrack() : SourceMediaTrack(MediaSegment::VIDEO, 90000) {}

  MOCK_METHOD(TrackTime, AppendData, (MediaSegment*, MediaSegment*),
              (override));

  void AddListener(MediaTrackListener* aListener) override {}
  RefPtr<GenericPromise> RemoveListener(
      MediaTrackListener* aListener) override {
    return GenericPromise::CreateAndResolve(true, __func__);
  }
  void Destroy() override { mDestroyed = true; }
};

// A frame libwebrtc decodes itself reaches the track as an I420 copy in the
// color space the frame declares.
TEST(MediaPipelineReceiveVideoTest, TestI420FrameColorSpace)
{
  MediaEventProducerExc<MediaPacket> rtcpSendEvent;
  auto conduit = MakeRefPtr<testing::NiceMock<MockVideoConduit>>();
  ON_CALL(*conduit, ReceiverRtcpSendEvent())
      .WillByDefault(testing::ReturnRef(rtcpSendEvent));
  RefPtr<VideoRenderer> renderer;
  EXPECT_CALL(*conduit, AttachRenderer(testing::_))
      .WillOnce(testing::DoAll(testing::SaveArg<0>(&renderer),
                               testing::Return(kMediaConduitNoError)));
  EXPECT_CALL(*conduit, DetachRenderer());

  auto source = MakeRefPtr<testing::NiceMock<MockSourceMediaTrack>>();
  RefPtr<layers::Image> image;
  ON_CALL(*source, AppendData(testing::_, testing::_))
      .WillByDefault([&](MediaSegment* aSegment, MediaSegment*) {
        auto* segment = static_cast<VideoSegment*>(aSegment);
        image = segment->GetLastFrame()->GetImage();
        return segment->GetDuration();
      });

  auto pipeline = MakeRefPtr<MediaPipelineReceiveVideo>(
      "", nullptr, AbstractThread::MainThread(), GetCurrentSerialEventTarget(),
      conduit, source, TrackingId(), PRINCIPAL_HANDLE_NONE,
      PrincipalPrivacy::NonPrivate);
  ASSERT_TRUE(renderer);

  auto receive = [&](const std::optional<webrtc::ColorSpace>& aColorSpace) {
    image = nullptr;
    renderer->RenderVideoFrame(
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(webrtc::I420Buffer::Create(16, 16))
            .set_color_space(aColorSpace)
            .build());
    MOZ_RELEASE_ASSERT(image);
    RefPtr<layers::PlanarYCbCrImage> yuvImage = image->AsPlanarYCbCrImage();
    MOZ_RELEASE_ASSERT(yuvImage);
    return *yuvImage->GetData();
  };

  {
    // A frame that declares no color space (e.g. VP8): BT.601 limited.
    const layers::PlanarYCbCrData data = receive(std::nullopt);
    EXPECT_EQ(data.mYUVColorSpace, gfx::YUVColorSpace::BT601);
    EXPECT_EQ(data.mColorRange, gfx::ColorRange::LIMITED);
    EXPECT_EQ(data.mColorPrimaries, gfx::ColorSpace2::UNKNOWN);
    EXPECT_EQ(data.mTransferFunction, gfx::TransferFunction::BT709);
  }
  {
    const layers::PlanarYCbCrData data =
        receive(webrtc::ColorSpace(webrtc::ColorSpace::PrimaryID::kBT709,
                                   webrtc::ColorSpace::TransferID::kBT709,
                                   webrtc::ColorSpace::MatrixID::kBT709,
                                   webrtc::ColorSpace::RangeID::kLimited));
    EXPECT_EQ(data.mYUVColorSpace, gfx::YUVColorSpace::BT709);
    EXPECT_EQ(data.mColorRange, gfx::ColorRange::LIMITED);
    EXPECT_EQ(data.mColorPrimaries, gfx::ColorSpace2::BT709);
    EXPECT_EQ(data.mTransferFunction, gfx::TransferFunction::BT709);
  }
  {
    const layers::PlanarYCbCrData data =
        receive(webrtc::ColorSpace(webrtc::ColorSpace::PrimaryID::kBT2020,
                                   webrtc::ColorSpace::TransferID::kSMPTEST2084,
                                   webrtc::ColorSpace::MatrixID::kBT2020_NCL,
                                   webrtc::ColorSpace::RangeID::kFull));
    EXPECT_EQ(data.mYUVColorSpace, gfx::YUVColorSpace::BT2020);
    EXPECT_EQ(data.mColorRange, gfx::ColorRange::FULL);
    EXPECT_EQ(data.mColorPrimaries, gfx::ColorSpace2::BT2020);
    EXPECT_EQ(data.mTransferFunction, gfx::TransferFunction::PQ);
  }
  {
    const layers::PlanarYCbCrData data = receive(
        webrtc::ColorSpace(webrtc::ColorSpace::PrimaryID::kBT2020,
                           webrtc::ColorSpace::TransferID::kARIB_STD_B67,
                           webrtc::ColorSpace::MatrixID::kBT2020_NCL,
                           webrtc::ColorSpace::RangeID::kLimited));
    EXPECT_EQ(data.mYUVColorSpace, gfx::YUVColorSpace::BT2020);
    EXPECT_EQ(data.mColorRange, gfx::ColorRange::LIMITED);
    EXPECT_EQ(data.mColorPrimaries, gfx::ColorSpace2::BT2020);
    EXPECT_EQ(data.mTransferFunction, gfx::TransferFunction::HLG);
  }
  {
    // An unspecified matrix follows the primaries, as for the platform
    // decoders.
    const layers::PlanarYCbCrData data = receive(
        webrtc::ColorSpace(webrtc::ColorSpace::PrimaryID::kBT709,
                           webrtc::ColorSpace::TransferID::kIEC61966_2_1,
                           webrtc::ColorSpace::MatrixID::kUnspecified,
                           webrtc::ColorSpace::RangeID::kFull));
    EXPECT_EQ(data.mYUVColorSpace, gfx::YUVColorSpace::BT709);
    EXPECT_EQ(data.mColorRange, gfx::ColorRange::FULL);
    EXPECT_EQ(data.mColorPrimaries, gfx::ColorSpace2::BT709);
    EXPECT_EQ(data.mTransferFunction, gfx::TransferFunction::SRGB);
  }
  {
    // With nothing to go by, BT.601 limited.
    const layers::PlanarYCbCrData data =
        receive(webrtc::ColorSpace(webrtc::ColorSpace::PrimaryID::kUnspecified,
                                   webrtc::ColorSpace::TransferID::kUnspecified,
                                   webrtc::ColorSpace::MatrixID::kUnspecified,
                                   webrtc::ColorSpace::RangeID::kInvalid));
    EXPECT_EQ(data.mYUVColorSpace, gfx::YUVColorSpace::BT601);
    EXPECT_EQ(data.mColorRange, gfx::ColorRange::LIMITED);
    EXPECT_EQ(data.mColorPrimaries, gfx::ColorSpace2::UNKNOWN);
    EXPECT_EQ(data.mTransferFunction, gfx::TransferFunction::BT709);
  }

  pipeline->Shutdown();
  NS_ProcessPendingEvents(nullptr);
  source->Destroy();
}

}  // end namespace
