/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef VIDEO_SESSION_H_
#define VIDEO_SESSION_H_

#include "MediaConduitInterface.h"
#include "RtpRtcpConfig.h"
#include "RunningStat.h"
#include "modules/rtp_rtcp/source/source_tracker.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/DataMutex.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/UniquePtr.h"

// conflicts with #include of scoped_ptr.h
#undef FF
// Video Engine Includes
#include "api/media_stream_interface.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
/** This file hosts several structures identifying different aspects
 * of a RTP Session.
 */

namespace mozilla {

// Convert (SI) kilobits/sec to (SI) bits/sec
#define KBPS(kbps) ((kbps) * 1000)

const int kViEMinCodecBitrate_bps = KBPS(30);
const unsigned int kVideoMtu = 1200;
const int kQpMax = 56;

template <typename T>
T MinIgnoreZero(const T& a, const T& b) {
  return std::min(a ? a : b, b ? b : a);
}

class VideoStreamFactory;
class WebrtcAudioConduit;
class WebrtcVideoConduit;

// Interface of external video encoder for WebRTC.
class WebrtcVideoEncoder : public VideoEncoder, public webrtc::VideoEncoder {};

// Interface of external video decoder for WebRTC.
class WebrtcVideoDecoder : public VideoDecoder, public webrtc::VideoDecoder {};

class RecvSinkProxy : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  explicit RecvSinkProxy(WebrtcVideoConduit* aOwner) : mOwner(aOwner) {}

 private:
  void OnFrame(const webrtc::VideoFrame& aFrame) override;

  WebrtcVideoConduit* const mOwner;
};

class SendSinkProxy : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  explicit SendSinkProxy(WebrtcVideoConduit* aOwner) : mOwner(aOwner) {}

 private:
  void OnFrame(const webrtc::VideoFrame& aFrame) override;

  WebrtcVideoConduit* const mOwner;
};

/**
 * Concrete class for Video session. Hooks up
 *  - media-source and target to external transport
 */
class WebrtcVideoConduit : public VideoSessionConduit,
                           public webrtc::RtcpEventObserver {
  friend class RecvSinkProxy;
  friend class SendSinkProxy;

 public:
  // Returns true when both encoder and decoder are HW accelerated.
  static bool HasH264Hardware();
  // Returns true when AV1 is supported in the build.
  static bool HasAv1();

  Maybe<int> ActiveSendPayloadType() const override;
  Maybe<int> ActiveRecvPayloadType() const override;

  /**
   * Function to attach Renderer end-point for the Media-Video conduit.
   * @param aRenderer : Reference to the concrete mozilla Video renderer
   * implementation Note: Multiple invocations of this API shall remove an
   * existing renderer and attaches the new to the Conduit.
   */
  MediaConduitErrorCode AttachRenderer(
      RefPtr<mozilla::VideoRenderer> aVideoRenderer) override;
  void DetachRenderer() override;

  Maybe<uint16_t> RtpSendBaseSeqFor(uint32_t aSsrc) const override;

  const dom::RTCStatsTimestampMaker& GetTimestampMaker() const override;

  void StopTransmitting();
  void StartTransmitting();
  void StopReceiving();
  void StartReceiving();

  void SetTrackSource(webrtc::VideoTrackSourceInterface* aSource) override;

  bool LockScaling() const override { return mLockScaling; }

  bool SendRtp(const uint8_t* aData, size_t aLength,
               const webrtc::PacketOptions& aOptions) override;
  bool SendSenderRtcp(const uint8_t* aData, size_t aLength) override;
  bool SendReceiverRtcp(const uint8_t* aData, size_t aLength) override;

  void OnRecvFrame(const webrtc::VideoFrame& aFrame);

  /**
   * Function to observe a video frame that was just passed to libwebrtc for
   * encoding and transport.
   *
   * Note that this is called async while the call to libwebrtc is sync, to
   * avoid a deadlock because webrtc::VideoBroadcaster holds its lock while
   * calling mSendSinkProxy, and this function locks mMutex. DeleteSendStream
   * locks those locks in reverse order.
   */
  void OnSendFrame(const webrtc::VideoFrame& aFrame);

  bool HasCodecPluginID(uint64_t aPluginID) const override;

  RefPtr<GenericPromise> Shutdown() override;

  // Call thread only.
  bool IsShutdown() const override;

  bool Denoising() const { return mDenoising; }

  uint8_t SpatialLayers() const { return mSpatialLayers; }

  uint8_t TemporalLayers() const { return mTemporalLayers; }

  webrtc::VideoCodecMode CodecMode() const;

  webrtc::DegradationPreference DegradationPreference() const;

  WebrtcVideoConduit(RefPtr<WebrtcCallWrapper> aCall,
                     nsCOMPtr<nsISerialEventTarget> aStsThread,
                     Options aOptions, std::string aPCHandle,
                     const TrackingId& aRecvTrackingId);
  virtual ~WebrtcVideoConduit();

  // Don't allow copying/assigning.
  WebrtcVideoConduit(const WebrtcVideoConduit&) = delete;
  void operator=(const WebrtcVideoConduit&) = delete;

  // Call thread.
  void InitControl(VideoConduitControlInterface* aControl) override;

  // Called when a parameter in mControl has changed. Call thread.
  void OnControlConfigChange();

  // Necessary Init steps on main thread.
  MediaConduitErrorCode Init();

  Ssrcs GetLocalSSRCs() const override;
  Maybe<Ssrc> GetAssociatedLocalRtxSSRC(Ssrc aSsrc) const override;
  Maybe<Ssrc> GetAssociatedRemoteRtxSSRC() const override;
  Maybe<Ssrc> GetRemoteSSRC() const override;

  Maybe<gfx::IntSize> GetLastResolution() const override;

  // Call thread.
  void UnsetRemoteSSRC(uint32_t aSsrc) override;

  static unsigned ToLibwebrtcMaxFramerate(const Maybe<double>& aMaxFramerate);

 private:
  void NotifyUnsetCurrentRemoteSSRC();
  void SetRemoteSSRCConfig(uint32_t aSsrc, uint32_t aRtxSsrc);
  void SetRemoteSSRCAndRestartAsNeeded(uint32_t aSsrc, uint32_t aRtxSsrc);
  webrtc::RefCountedObject<mozilla::VideoStreamFactory>*
  CreateVideoStreamFactory();

 public:
  // Creating a recv stream or a send stream requires a local ssrc to be
  // configured. This method will generate one if needed.
  void EnsureLocalSSRC();
  // Creating a recv stream requires a remote ssrc to be configured. This method
  // will generate one if needed.
  void EnsureRemoteSSRC();

  Maybe<webrtc::VideoReceiveStreamInterface::Stats> GetReceiverStats()
      const override;
  Maybe<webrtc::VideoSendStream::Stats> GetSenderStats() const override;
  Maybe<webrtc::Call::Stats> GetCallStats() const override;

  bool AddFrameHistory(dom::Sequence<dom::RTCVideoFrameHistoryInternal>*
                           outHistories) const override;

  void SetJitterBufferTarget(DOMHighResTimeStamp aTargetMs) override;

  uint64_t MozVideoLatencyAvg();

  void DisableSsrcChanges() override {
    MOZ_ASSERT(mCallThread->IsOnCurrentThread());
    mAllowSsrcChange = false;
  }

  void CollectTelemetryData() override;

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
        aEvent.Connect(mCallThread, this, &WebrtcVideoConduit::OnRtpReceived);
  }
  void ConnectReceiverRtcpEvent(
      MediaEventSourceExc<webrtc::CopyOnWriteBuffer>& aEvent) override {
    mReceiverRtcpEventListener =
        aEvent.Connect(mCallThread, this, &WebrtcVideoConduit::OnRtcpReceived);
  }
  void ConnectSenderRtcpEvent(
      MediaEventSourceExc<webrtc::CopyOnWriteBuffer>& aEvent) override {
    mSenderRtcpEventListener =
        aEvent.Connect(mCallThread, this, &WebrtcVideoConduit::OnRtcpReceived);
  }

  AbstractCanonical<Maybe<gfx::IntSize>>* CanonicalReceivingSize() override {
    return &mReceivingSize;
  }

  const std::vector<webrtc::RtpSource>& GetUpstreamRtpSources() const override;

  void RequestKeyFrame(FrameTransformerProxy* aProxy) override;
  void GenerateKeyFrame(const Maybe<std::string>& aRid,
                        FrameTransformerProxy* aProxy) override;
  void GenerateKeyFrames(std::vector<std::string> aRids) override;

 private:
  // Utility function to dump recv codec database
  void DumpCodecDB() const;

  // Video Latency Test averaging filter
  void VideoLatencyUpdate(uint64_t aNewSample);

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

  void DeliverPacket(webrtc::CopyOnWriteBuffer packet,
                     PacketType type) override;

  MediaEventSource<void>& RtcpByeEvent() override { return mRtcpByeEvent; }
  MediaEventSource<void>& RtcpTimeoutEvent() override {
    return mRtcpTimeoutEvent;
  }
  MediaEventSource<void>& RtpPacketEvent() override { return mRtpPacketEvent; }

  bool RequiresNewSendStream(const VideoCodecConfig& newConfig) const;

  mutable mozilla::ReentrantMonitor mRendererMonitor MOZ_UNANNOTATED;

  // Accessed on any thread under mRendererMonitor.
  RefPtr<mozilla::VideoRenderer> mRenderer;

  // WEBRTC.ORG Call API
  // Const so can be accessed on any thread. All methods are called on the Call
  // thread.
  const RefPtr<WebrtcCallWrapper> mCall;

  // Call worker thread. All access to mCall->Call() happens here.
  const nsCOMPtr<nsISerialEventTarget> mCallThread;

  // Socket transport service thread that runs stats queries against us. Any
  // thread.
  const nsCOMPtr<nsISerialEventTarget> mStsThread;

  // XXX
  const RefPtr<AbstractThread> mFrameRecvThread;

  // Thread on which we are fed video frames. Set lazily on first call to
  // SendVideoFrame().
  nsCOMPtr<nsISerialEventTarget> mFrameSendingThread;

  struct Control {
    // Mirrors that map to VideoConduitControlInterface for control. Call thread
    // only.
    Mirror<bool> mReceiving;
    Mirror<bool> mTransmitting;
    Mirror<Ssrcs> mLocalSsrcs;
    Mirror<Ssrcs> mLocalRtxSsrcs;
    Mirror<std::string> mLocalCname;
    Mirror<std::string> mMid;
    Mirror<Ssrc> mRemoteSsrc;
    Mirror<Ssrc> mRemoteRtxSsrc;
    Mirror<std::string> mSyncGroup;
    Mirror<RtpExtList> mLocalRecvRtpExtensions;
    Mirror<RtpExtList> mLocalSendRtpExtensions;
    Mirror<Maybe<VideoCodecConfig>> mSendCodec;
    Mirror<Maybe<RtpRtcpConfig>> mSendRtpRtcpConfig;
    Mirror<std::vector<VideoCodecConfig>> mRecvCodecs;
    Mirror<Maybe<RtpRtcpConfig>> mRecvRtpRtcpConfig;
    Mirror<webrtc::VideoCodecMode> mCodecMode;
    Mirror<RefPtr<FrameTransformerProxy>> mFrameTransformerProxySend;
    Mirror<RefPtr<FrameTransformerProxy>> mFrameTransformerProxyRecv;
    Mirror<webrtc::DegradationPreference> mVideoDegradationPreference;

    // For caching mRemoteSsrc and mRemoteRtxSsrc, since another caller may
    // change the remote ssrc in the stream config directly.
    Ssrc mConfiguredRemoteSsrc = 0;
    Ssrc mConfiguredRemoteRtxSsrc = 0;
    // For tracking changes to mSendCodec and mSendRtpRtcpConfig.
    Maybe<VideoCodecConfig> mConfiguredSendCodec;
    Maybe<RtpRtcpConfig> mConfiguredSendRtpRtcpConfig;
    // For tracking changes to mRecvCodecs and mRecvRtpRtcpConfig.
    std::vector<VideoCodecConfig> mConfiguredRecvCodecs;
    Maybe<RtpRtcpConfig> mConfiguredRecvRtpRtcpConfig;
    // For tracking changes to mVideoDegradationPreference
    webrtc::DegradationPreference mConfiguredDegradationPreference =
        webrtc::DegradationPreference::DISABLED;

    // For change tracking. Callthread only.
    RefPtr<FrameTransformerProxy> mConfiguredFrameTransformerProxySend;
    RefPtr<FrameTransformerProxy> mConfiguredFrameTransformerProxyRecv;

    Control() = delete;
    explicit Control(const RefPtr<AbstractThread>& aCallThread);
  } mControl;

  // Canonical for mirroring mReceivingWidth and mReceivingHeight. Call thread
  // only.
  Canonical<Maybe<gfx::IntSize>> mReceivingSize;

  // WatchManager allowing Mirrors and other watch targets to trigger functions
  // that will update the webrtc.org configuration.
  WatchManager<WebrtcVideoConduit> mWatchManager;

  mutable Mutex mMutex MOZ_UNANNOTATED;

  // Decoder factory used by mRecvStream when it needs new decoders. This is
  // not shared broader like some state in the WebrtcCallWrapper because it
  // handles CodecPluginID plumbing tied to this VideoConduit.
  const UniquePtr<WebrtcVideoDecoderFactory> mDecoderFactory;

  // Encoder factory used by mSendStream when it needs new encoders. This is
  // not shared broader like some state in the WebrtcCallWrapper because it
  // handles CodecPluginID plumbing tied to this VideoConduit.
  const UniquePtr<WebrtcVideoEncoderFactory> mEncoderFactory;

  // These sink proxies are needed because both the recv and send sides of the
  // conduit need to implement webrtc::VideoSinkInterface<webrtc::VideoFrame>.
  RecvSinkProxy mRecvSinkProxy;
  SendSinkProxy mSendSinkProxy;

  // The track source that passes video frames to the libwebrtc send stream, and
  // to mSendSinkProxy.
  RefPtr<webrtc::VideoTrackSourceInterface> mTrackSource;

  // Engine state we are concerned with. Written on the Call thread and read
  // anywhere.
  mozilla::Atomic<bool>
      mEngineTransmitting;  // If true ==> Transmit Subsystem is up and running
  mozilla::Atomic<bool>
      mEngineReceiving;  // if true ==> Receive Subsystem up and running

  // Written only on the Call thread. Guarded by mMutex, except for reads on the
  // Call thread.
  Maybe<VideoCodecConfig> mCurSendCodecConfig;

  // Bookkeeping of stats for telemetry. Call thread only.
  RunningStat mSendFramerate;
  RunningStat mSendBitrate;
  RunningStat mRecvFramerate;
  RunningStat mRecvBitrate;

  // Must call webrtc::Call::DestroyVideoReceive/SendStream to delete this.
  // Written only on the Call thread. Guarded by mMutex, except for reads on the
  // Call thread.
  webrtc::VideoReceiveStreamInterface* mRecvStream = nullptr;

  // Fed by mRecvStreamConfig.on_frame_delivered_callback. Call thread only,
  // since webrtc::SourceTracker itself is not thread-safe.
  webrtc::SourceTracker mSourceTracker;

  // Must call webrtc::Call::DestroyVideoReceive/SendStream to delete this.
  webrtc::VideoSendStream* mSendStream = nullptr;

  // Size of the most recently sent video frame. Call thread only.
  Maybe<gfx::IntSize> mLastSize;

  // Written on the frame feeding thread, the timestamp of the last frame on the
  // send side. This is a local timestamp using the system clock with an
  // unspecified epoch (Like mozilla::TimeStamp). Guarded by mMutex.
  Maybe<webrtc::Timestamp> mLastTimestampSend;

  // Written on the frame receive thread, the rtp timestamp of the last frame
  // on the receive side, in 90kHz base. This comes from the RTP packet.
  // Guarded by mMutex.
  Maybe<uint32_t> mLastRTPTimestampReceive;

  // Accessed from any thread under mRendererMonitor.
  uint64_t mVideoLatencyAvg = 0;

  const bool mVideoLatencyTestEnable;

  // All in bps.
  const int mMinBitrate;
  const int mStartBitrate;
  const int mPrefMaxBitrate;
  const int mMinBitrateEstimate;

  // Max bitrate in bps as provided by negotiation. Call thread only.
  int mNegotiatedMaxBitrate = 0;

  // Set to true to force denoising on.
  const bool mDenoising;

  // Set to true to ignore sink wants (scaling due to bwe and cpu usage) and
  // degradation preference (always use MAINTAIN_RESOLUTION).
  const bool mLockScaling;

  const uint8_t mSpatialLayers;
  const uint8_t mTemporalLayers;

  static const unsigned int sAlphaNum = 7;
  static const unsigned int sAlphaDen = 8;
  static const unsigned int sRoundingPadding = 1024;

  // Target jitter buffer to be applied to the receive stream in milliseconds.
  uint16_t mJitterBufferTargetMs = 0;

  // Set up in the ctor and then not touched. Called through by the streams on
  // any thread. Safe since we own and control the lifetime of the streams.
  WebrtcSendTransport mSendTransport;
  WebrtcReceiveTransport mRecvTransport;

  // Written only on the Call thread. Guarded by mMutex, except for reads on the
  // Call thread. Typical non-Call thread access is on the frame delivery
  // thread.
  webrtc::VideoSendStream::Config mSendStreamConfig;

  // Call thread only.
  webrtc::VideoEncoderConfig mEncoderConfig;

  // Written only on the Call thread. Guarded by mMutex, except for reads on the
  // Call thread. Calls can happen under mMutex on any thread.
  DataMutex<RefPtr<webrtc::RefCountedObject<VideoStreamFactory>>>
      mVideoStreamFactory;

  // Call thread only.
  webrtc::VideoReceiveStreamInterface::Config mRecvStreamConfig;

  // Are SSRC changes without signaling allowed or not.
  // Call thread only.
  bool mAllowSsrcChange = true;

  // Accessed during configuration/signaling (Call thread), and on the frame
  // delivery thread for frame history tracking. Set only on the Call thread.
  Atomic<uint32_t> mRecvSSRC =
      Atomic<uint32_t>(0);  // this can change during a stream!

  // Accessed from both the STS and frame delivery thread for frame history
  // tracking. Set when receiving packets.
  Atomic<uint32_t> mRemoteSendSSRC =
      Atomic<uint32_t>(0);  // this can change during a stream!

  // Main thread only
  nsTArray<uint64_t> mSendCodecPluginIDs;
  // Main thread only
  nsTArray<uint64_t> mRecvCodecPluginIDs;

  // Main thread only
  MediaEventListener mSendPluginCreated;
  MediaEventListener mSendPluginReleased;
  MediaEventListener mRecvPluginCreated;
  MediaEventListener mRecvPluginReleased;

  // Call thread only. ssrc -> base_seq
  std::map<uint32_t, uint16_t> mRtpSendBaseSeqs;
  // libwebrtc network thread only. ssrc -> base_seq.
  // To track changes needed to mRtpSendBaseSeqs.
  std::map<uint32_t, uint16_t> mRtpSendBaseSeqs_n;

  // Tracking the attributes of received frames over time
  // Protected by mRendererMonitor
  dom::RTCVideoFrameHistoryInternal mReceivedFrameHistory;

  // Call thread only.
  Canonical<std::vector<webrtc::RtpSource>> mCanonicalRtpSources;

  // Main thread only mirror of mCanonicalRtpSources.
  Mirror<std::vector<webrtc::RtpSource>> mRtpSources;

  // Cache of stats that holds the send stream stats during the stream
  // recreation process. After DeleteSendStream() then CreateSendStream() and
  // before the codecs are initialized there is a gap where the send stream
  // stats have no substreams. This holds onto the stats until the codecs are
  // initialized and the send stream is recreated.
  // It is mutable because we want to be able to invalidate the cache when a
  // GetStats call is made.
  // Call thread only.
  mutable Maybe<webrtc::VideoSendStream::Stats> mTransitionalSendStreamStats;

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
