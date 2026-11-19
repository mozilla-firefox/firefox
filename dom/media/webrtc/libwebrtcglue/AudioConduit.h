/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AUDIO_SESSION_H_
#define AUDIO_SESSION_H_

#include "MediaConduitInterface.h"
#include "modules/rtp_rtcp/source/source_tracker.h"
#include "mozilla/Attributes.h"
#include "mozilla/RWLock.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/TimeStamp.h"

/**
 * This file hosts several structures identifying different aspects of a RTP
 * Session.
 */
namespace mozilla {

struct DtmfEvent;

/**
 * Concrete class for Audio session. Hooks up
 *  - media-source and target to external transport
 */
class WebrtcAudioConduit : public AudioSessionConduit,
                           public webrtc::RtcpEventObserver {
 public:
  Maybe<int> ActiveSendPayloadType() const override;
  Maybe<int> ActiveRecvPayloadType() const override;

  void OnRtpReceived(webrtc::RtpPacketReceived&& aPacket,
                     webrtc::RTPHeader&& aHeader);
  void OnRtcpReceived(webrtc::CopyOnWriteBuffer&& aPacket);

  void OnRtcpBye() override;
  void OnRtcpTimeout() override;

  void SetTransportActive(bool aActive) override;

  MediaEventSourceExc<MediaPacket>& SenderRtpSendEvent() override {
    return mSenderRtpSendEvent;
  }
  MediaEventSourceExc<MediaPacket>& SenderRtcpSendEvent() override {
    return mSenderRtcpSendEvent;
  }
  MediaEventSourceExc<MediaPacket>& ReceiverRtcpSendEvent() override {
    return mReceiverRtcpSendEvent;
  }
  void ConnectReceiverRtpEvent(
      MediaEventSourceExc<webrtc::RtpPacketReceived, webrtc::RTPHeader>& aEvent)
      override {
    mReceiverRtpEventListener =
        aEvent.Connect(mCallThread, this, &WebrtcAudioConduit::OnRtpReceived);
  }
  void ConnectReceiverRtcpEvent(
      MediaEventSourceExc<webrtc::CopyOnWriteBuffer>& aEvent) override {
    mReceiverRtcpEventListener =
        aEvent.Connect(mCallThread, this, &WebrtcAudioConduit::OnRtcpReceived);
  }
  void ConnectSenderRtcpEvent(
      MediaEventSourceExc<webrtc::CopyOnWriteBuffer>& aEvent) override {
    mSenderRtcpEventListener =
        aEvent.Connect(mCallThread, this, &WebrtcAudioConduit::OnRtcpReceived);
  }

  Maybe<uint16_t> RtpSendBaseSeqFor(uint32_t aSsrc) const override;

  const dom::RTCStatsTimestampMaker& GetTimestampMaker() const override;

  void StopTransmitting();
  void StartTransmitting();
  void StopReceiving();
  void StartReceiving();

  /**
   * Function to deliver externally captured audio sample for encoding and
   * transport
   * @param frame [in]: AudioFrame in upstream's format for forwarding to the
   *                    send stream. Ownership is passed along.
   * NOTE: ConfigureSendMediaCodec() SHOULD be called before this function can
   * be invoked. This ensures the inserted audio-samples can be transmitted by
   * the conduit.
   */
  MediaConduitErrorCode SendAudioFrame(
      std::unique_ptr<webrtc::AudioFrame> frame) override;

  /**
   * Function to grab a decoded audio-sample from the media engine for
   * rendering / playout of length 10 milliseconds.
   *
   * @param samplingFreqHz [in]: Frequency of the sampling for playback in
   *                             Hertz (16000, 32000,..)
   * @param frame [in/out]: Pointer to an AudioFrame to which audio data will be
   *                        copied
   * NOTE: This function should be invoked every 10 milliseconds for the best
   *       performance
   * NOTE: ConfigureRecvMediaCodec() SHOULD be called before this function can
   *       be invoked
   * This ensures the decoded samples are ready for reading and playout is
   * enabled.
   */
  MediaConduitErrorCode GetAudioFrame(int32_t samplingFreqHz,
                                      webrtc::AudioFrame* frame) override;

  bool SendRtp(const uint8_t* aData, size_t aLength,
               const webrtc::PacketOptions& aOptions) override;
  bool SendSenderRtcp(const uint8_t* aData, size_t aLength) override;
  bool SendReceiverRtcp(const uint8_t* aData, size_t aLength) override;

  bool HasCodecPluginID(uint64_t aPluginID) const override { return false; }

  void SetJitterBufferTarget(DOMHighResTimeStamp aTargetMs) override;

  void DeliverPacket(webrtc::CopyOnWriteBuffer packet,
                     PacketType type) override;

  RefPtr<GenericPromise> Shutdown() override;

  // Call thread only.
  bool IsShutdown() const override;

  WebrtcAudioConduit(RefPtr<WebrtcCallWrapper> aCall,
                     nsCOMPtr<nsISerialEventTarget> aStsThread);

  virtual ~WebrtcAudioConduit();

  WebrtcAudioConduit(const WebrtcAudioConduit& other) = delete;
  void operator=(const WebrtcAudioConduit& other) = delete;

  // Call thread.
  void InitControl(AudioConduitControlInterface* aControl) override;

  // Necessary Init steps on main thread.
  MediaConduitErrorCode Init();

  // Handle a DTMF event from mControl.mOnDtmfEventListener.
  void OnDtmfEvent(const DtmfEvent& aEvent);

  // Called when a parameter in mControl has changed. Call thread.
  void OnControlConfigChange();

  Ssrcs GetLocalSSRCs() const override;
  Maybe<Ssrc> GetRemoteSSRC() const override;

  void DisableSsrcChanges() override {
    MOZ_ASSERT(mCallThread->IsOnCurrentThread());
    mAllowSsrcChange = false;
  }

 private:
  /**
   * Override the remote ssrc configured on mRecvStreamConfig.
   *
   * Recreates and restarts the recv stream if needed. The overridden value is
   * overwritten the next time the mControl.mRemoteSsrc mirror changes value.
   *
   * Call thread only.
   */
  bool OverrideRemoteSSRC(uint32_t aSsrc);

 public:
  void UnsetRemoteSSRC(uint32_t aSsrc) override {}

  Maybe<webrtc::AudioReceiveStreamInterface::Stats> GetReceiverStats()
      const override;
  Maybe<webrtc::AudioSendStream::Stats> GetSenderStats() const override;
  Maybe<webrtc::Call::Stats> GetCallStats() const override;

  bool IsSamplingFreqSupported(int freq) const override;

  MediaEventSource<void>& RtcpByeEvent() override { return mRtcpByeEvent; }
  MediaEventSource<void>& RtcpTimeoutEvent() override {
    return mRtcpTimeoutEvent;
  }
  MediaEventSource<void>& RtpPacketEvent() override { return mRtpPacketEvent; }

  const std::vector<webrtc::RtpSource>& GetUpstreamRtpSources() const override;

 private:
  // Generate block size in sample length for a given sampling frequency
  unsigned int GetNum10msSamplesForFrequency(int samplingFreqHz) const;

  // Checks the codec to be applied
  static MediaConduitErrorCode ValidateCodecConfig(
      const AudioCodecConfig& codecInfo, bool send);
  /**
   * Of all extensions in aExtensions, returns a list of supported extensions.
   */
  static RtpExtList FilterExtensions(
      MediaSessionConduitLocalDirection aDirection,
      const RtpExtList& aExtensions);
  static webrtc::SdpAudioFormat CodecConfigToLibwebrtcFormat(
      const AudioCodecConfig& aConfig);

  // Call thread only, called before DeleteSendStream if streams need recreation
  void MemoSendStreamStats();

  void CreateSendStream();
  void DeleteSendStream();
  void CreateRecvStream();
  void DeleteRecvStream();

  // Call thread only.
  // Should only be called from Shutdown()
  void SetIsShutdown();

  // Call thread only. Tells the call whether our transport can carry media.
  void UpdateNetworkState(bool aReady);

  // Are SSRC changes without signaling allowed or not.
  // Call thread only.
  bool mAllowSsrcChange = true;

  // Const so can be accessed on any thread. Most methods are called on the Call
  // thread.
  const RefPtr<WebrtcCallWrapper> mCall;

  // Set up in the ctor and then not touched. Called through by the streams on
  // any thread.
  WebrtcSendTransport mSendTransport;
  WebrtcReceiveTransport mRecvTransport;

  // Accessed only on the Call thread.
  webrtc::AudioReceiveStreamInterface::Config mRecvStreamConfig;

  // Written only on the Call thread. Guarded by mLock, except for reads on the
  // Call thread.
  webrtc::AudioReceiveStreamInterface* mRecvStream;

  // Fed by mRecvStreamConfig.on_frame_delivered_callback. Call thread only,
  // since webrtc::SourceTracker itself is not thread-safe.
  webrtc::SourceTracker mSourceTracker;

  // Accessed only on the Call thread.
  webrtc::AudioSendStream::Config mSendStreamConfig;

  // Written only on the Call thread. Guarded by mLock, except for reads on the
  // Call thread.
  webrtc::AudioSendStream* mSendStream;

  // If true => mSendStream started and not stopped
  // Written only on the Call thread.
  Atomic<bool> mSendStreamRunning;
  // If true => mRecvStream started and not stopped
  // Written only on the Call thread.
  Atomic<bool> mRecvStreamRunning;

  // Accessed only on the Call thread.
  bool mDtmfEnabled;

  mutable RWLock mLock MOZ_UNANNOTATED;

  // Call worker thread. All access to mCall->Call() happens here.
  const RefPtr<AbstractThread> mCallThread;

  // Socket transport service thread. Any thread.
  const nsCOMPtr<nsISerialEventTarget> mStsThread;

  // Target jitter buffer to be applied to the receive stream in milliseconds.
  uint16_t mJitterBufferTargetMs = 0;

  struct Control {
    // Mirrors and events that map to AudioConduitControlInterface for control.
    // Call thread only.
    Mirror<bool> mReceiving;
    Mirror<bool> mTransmitting;
    Mirror<Ssrcs> mLocalSsrcs;
    Mirror<std::string> mLocalCname;
    Mirror<std::string> mMid;
    Mirror<Ssrc> mRemoteSsrc;
    Mirror<std::string> mSyncGroup;
    Mirror<RtpExtList> mLocalRecvRtpExtensions;
    Mirror<RtpExtList> mLocalSendRtpExtensions;
    Mirror<Maybe<AudioCodecConfig>> mSendCodec;
    Mirror<std::vector<AudioCodecConfig>> mRecvCodecs;
    Mirror<RefPtr<FrameTransformerProxy>> mFrameTransformerProxySend;
    Mirror<RefPtr<FrameTransformerProxy>> mFrameTransformerProxyRecv;
    MediaEventListener mOnDtmfEventListener;

    // For caching mRemoteSsrc, since another caller may change the remote ssrc
    // in the stream config directly.
    Ssrc mConfiguredRemoteSsrc = 0;
    // For tracking changes to mSendCodec.
    Maybe<AudioCodecConfig> mConfiguredSendCodec;
    // For tracking changes to mRecvCodecs.
    std::vector<AudioCodecConfig> mConfiguredRecvCodecs;

    // For change tracking. Callthread only.
    RefPtr<FrameTransformerProxy> mConfiguredFrameTransformerProxySend;
    RefPtr<FrameTransformerProxy> mConfiguredFrameTransformerProxyRecv;

    Control() = delete;
    explicit Control(const RefPtr<AbstractThread>& aCallThread);
  } mControl;

  // WatchManager allowing Mirrors to trigger functions that will update the
  // webrtc.org configuration.
  WatchManager<WebrtcAudioConduit> mWatchManager;

  // Accessed from mStsThread. Last successfully polled RTT
  Maybe<DOMHighResTimeStamp> mRttSec;

  // Call thread only. ssrc -> base_seq
  std::map<uint32_t, uint16_t> mRtpSendBaseSeqs;
  // libwebrtc network thread only. ssrc -> base_seq.
  // To track changes needed to mRtpSendBaseSeqs.
  std::map<uint32_t, uint16_t> mRtpSendBaseSeqs_n;

  // Call thread only.
  Canonical<std::vector<webrtc::RtpSource>> mCanonicalRtpSources;

  // Main thread only mirror of mCanonicalRtpSources.
  Mirror<std::vector<webrtc::RtpSource>> mRtpSources;

  // Stores stats between a call to DeleteSendStream and CreateSendStream so
  // that we can continue to report outbound-rtp stats while waiting for codec
  // initialization.
  // It is mutable because we want to be able to invalidate the cache when a
  // GetStats call is made.
  // Call thread only.
  mutable Maybe<webrtc::AudioSendStream::Stats> mTransitionalSendStreamStats;

  // Thread safe
  Atomic<bool> mTransportActive = Atomic<bool>(false);
  MediaEventProducer<void> mRtcpByeEvent;
  MediaEventProducer<void> mRtcpTimeoutEvent;
  MediaEventProducer<void> mRtpPacketEvent;
  MediaEventProducerExc<MediaPacket> mSenderRtpSendEvent;
  MediaEventProducerExc<MediaPacket> mSenderRtcpSendEvent;
  MediaEventProducerExc<MediaPacket> mReceiverRtcpSendEvent;

  // Assigned and revoked on mStsThread. Listeners for receiving packets.
  MediaEventListener mReceiverRtpEventListener;   // Rtp-receiving pipeline
  MediaEventListener mReceiverRtcpEventListener;  // Rctp-receiving pipeline
  MediaEventListener mSenderRtcpEventListener;    // Rctp-sending pipeline

  // Whether the conduit is shutdown or not.
  // Call thread only.
  bool mIsShutdown = false;

  // Whether we have told the call that our transport can carry media.
  // Call thread only.
  bool mNetworkReady = false;
};

}  // namespace mozilla

#endif
