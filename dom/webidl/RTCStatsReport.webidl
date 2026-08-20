/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://www.w3.org/TR/webrtc-stats/
 */

enum RTCStatsType {
  "codec",
  "inbound-rtp",
  "outbound-rtp",
  "remote-inbound-rtp",
  "remote-outbound-rtp",
  "media-source",
  "peer-connection",
  "csrc",
  "data-channel",
  "session",
  "track",
  "transport",
  "candidate-pair",
  "local-candidate",
  "remote-candidate",
  "certificate"
};

dictionary RTCStats {
  DOMHighResTimeStamp timestamp;
  RTCStatsType type;
  DOMString id;
};

dictionary RTCRtpStreamStats : RTCStats {
  required unsigned long ssrc;
  required DOMString kind;
  DOMString mediaType;
  DOMString transportId;
  DOMString codecId;
};

dictionary RTCCodecStats : RTCStats {
  required unsigned long payloadType;
  RTCCodecType  codecType;
  required DOMString     transportId;
  required DOMString     mimeType;
  unsigned long clockRate;
  unsigned long channels;
  DOMString     sdpFmtpLine;
};

enum RTCCodecType {
  "encode",
  "decode",
};

dictionary RTCReceivedRtpStreamStats: RTCRtpStreamStats {
  unsigned long long packetsReceived;
  long long packetsLost;
  double jitter;
  unsigned long discardedPackets; // non-standard alias for packetsDiscarded
  unsigned long packetsDiscarded;
};

dictionary RTCInboundRtpStreamStats : RTCReceivedRtpStreamStats {
  required DOMString trackIdentifier;
  DOMString mid;
  DOMString remoteId;
  unsigned long framesDecoded;
  unsigned long keyFramesDecoded;
  unsigned long framesDropped;
  unsigned long frameWidth;
  unsigned long frameHeight;
  double framesPerSecond;
  unsigned long long qpSum;
  double totalDecodeTime;
  double totalInterFrameDelay;
  double totalSquaredInterFrameDelay;
  unsigned long pauseCount;
  double totalPausesDuration;
  unsigned long freezeCount;
  double totalFreezesDuration;
  DOMHighResTimeStamp lastPacketReceivedTimestamp;
  unsigned long long headerBytesReceived;
  unsigned long long fecPacketsReceived;
  unsigned long long fecPacketsDiscarded;
  unsigned long long bytesReceived;
  unsigned long nackCount;
  unsigned long firCount;
  unsigned long pliCount;
  double totalProcessingDelay;
  DOMHighResTimeStamp estimatedPlayoutTimestamp;
  double jitterBufferDelay;
  unsigned long long jitterBufferEmittedCount;
  double jitterBufferTargetDelay;
  double jitterBufferMinimumDelay;
  unsigned long long totalSamplesReceived;
  unsigned long long concealedSamples;
  unsigned long long silentConcealedSamples;
  unsigned long long concealmentEvents;
  unsigned long long insertedSamplesForDeceleration;
  unsigned long long removedSamplesForAcceleration;
  double audioLevel;
  double totalAudioEnergy;
  double totalSamplesDuration;
  unsigned long framesReceived;
  unsigned long framesAssembledFromMultiplePackets;
  double totalAssemblyTime;
  unsigned long long retransmittedPacketsReceived;
  unsigned long long retransmittedBytesReceived;
  unsigned long rtxSsrc;
};

dictionary RTCRemoteInboundRtpStreamStats : RTCReceivedRtpStreamStats {
  DOMString localId;
  double roundTripTime;
  double totalRoundTripTime;
  double fractionLost;
  unsigned long long roundTripTimeMeasurements;
};

dictionary RTCSentRtpStreamStats : RTCRtpStreamStats {
  unsigned long packetsSent;
  unsigned long long bytesSent;
};

dictionary RTCOutboundRtpStreamStats : RTCSentRtpStreamStats {
  DOMString mid;
  DOMString remoteId;
  DOMString rid;
  unsigned long long headerBytesSent;
  unsigned long long retransmittedPacketsSent;
  unsigned long long retransmittedBytesSent;
  unsigned long rtxSsrc;
  unsigned long long totalEncodedBytesTarget;
  unsigned long frameWidth;
  unsigned long frameHeight;
  double framesPerSecond;
  unsigned long framesSent;
  unsigned long hugeFramesSent;
  unsigned long framesEncoded;
  unsigned long keyFramesEncoded;
  unsigned long long qpSum;
  double totalEncodeTime;
  unsigned long nackCount;
  unsigned long firCount;
  unsigned long pliCount;
};

dictionary RTCRemoteOutboundRtpStreamStats : RTCSentRtpStreamStats {
  DOMString localId;
  DOMHighResTimeStamp remoteTimestamp;
};

dictionary RTCMediaSourceStats : RTCStats {
  required DOMString trackIdentifier;
  required DOMString kind;
};

dictionary RTCVideoSourceStats : RTCMediaSourceStats {
  unsigned long   width;
  unsigned long   height;
  unsigned long   frames;
  double          framesPerSecond;
};

dictionary RTCPeerConnectionStats : RTCStats {
  unsigned long dataChannelsOpened;
  unsigned long dataChannelsClosed;
};

dictionary RTCRTPContributingSourceStats : RTCStats {
  unsigned long contributorSsrc;
  DOMString     inboundRtpStreamId;
};

dictionary RTCDataChannelStats : RTCStats {
  DOMString           label;
  DOMString           protocol;
  long                dataChannelIdentifier;
  RTCDataChannelState state;
  unsigned long       messagesSent;
  unsigned long long  bytesSent;
  unsigned long       messagesReceived;
  unsigned long long  bytesReceived;
};

enum RTCStatsIceCandidatePairState {
  "frozen",
  "waiting",
  "in-progress",
  "failed",
  "succeeded",
  "cancelled"
};

dictionary RTCIceCandidatePairStats : RTCStats {
  required DOMString transportId;
  DOMString localCandidateId;
  DOMString remoteCandidateId;
  RTCStatsIceCandidatePairState state;
  unsigned long long priority;
  boolean nominated;
  boolean writable; // removed from standard
  boolean readable; // removed from standard
  unsigned long long packetsSent;
  unsigned long long packetsReceived;
  unsigned long long bytesSent;
  unsigned long long bytesReceived;
  DOMHighResTimeStamp lastPacketSentTimestamp;
  DOMHighResTimeStamp lastPacketReceivedTimestamp;
  boolean selected; // removed from standard
  double totalRoundTripTime;
  double currentRoundTripTime;
  unsigned long long responsesReceived;
  [ChromeOnly]
  unsigned long componentId; // moz
};

dictionary RTCIceCandidateStats : RTCStats {
  required DOMString transportId;
  DOMString address;
  long port;
  DOMString protocol;
  RTCIceCandidateType candidateType;
  long priority;
  DOMString relayProtocol;
  DOMString foundation;
  DOMString usernameFragment;
  RTCIceTcpCandidateType tcpType;
  [ChromeOnly]
  DOMString proxied;
};

enum RTCDtlsRole {
  "client",
  "server",
  "unknown"
};

dictionary RTCTransportStats : RTCStats {
  unsigned long long packetsSent;
  unsigned long long packetsReceived;
  unsigned long long bytesSent;
  unsigned long long bytesReceived;
  RTCIceRole iceRole;
  DOMString iceLocalUsernameFragment;
  required RTCDtlsTransportState dtlsState;
  RTCIceTransportState iceState;
  DOMString selectedCandidatePairId;
  DOMString localCertificateId;
  DOMString remoteCertificateId;
  DOMString tlsVersion;
  DOMString dtlsCipher;
  RTCDtlsRole dtlsRole;
  DOMString srtpCipher;
  unsigned long selectedCandidatePairChanges;
};

dictionary RTCCertificateStats : RTCStats {
  required DOMString fingerprint;
  required DOMString fingerprintAlgorithm;
  required DOMString base64Certificate;
  // The id of the issuer certificate's stat in this report, not the
  // certificate's X.509 issuer field.
  DOMString issuerCertificateId;
};

// This is for tracking the frame rate in about:webrtc
dictionary RTCVideoFrameHistoryEntryInternal {
  required unsigned long       width;
  required unsigned long       height;
  required unsigned long       rotationAngle;
  required DOMHighResTimeStamp firstFrameTimestamp;
  required DOMHighResTimeStamp lastFrameTimestamp;
  required unsigned long long  consecutiveFrames;
  required unsigned long       localSsrc;
  required unsigned long       remoteSsrc;
};

// Collection over the entries for a single track for about:webrtc
dictionary RTCVideoFrameHistoryInternal {
  required DOMString                          trackIdentifier;
  sequence<RTCVideoFrameHistoryEntryInternal> entries = [];
};

// Collection over the libwebrtc bandwidth estimation stats
dictionary RTCBandwidthEstimationInternal {
  required DOMString  trackIdentifier;
  long                sendBandwidthBps;    // Estimated available send bandwidth
  long                maxPaddingBps;       // Cumulative configured max padding
  long                receiveBandwidthBps; // Estimated available receive bandwidth
  long                pacerDelayMs;
  long                rttMs;
};

// This is used by about:webrtc to report SDP parsing errors
dictionary RTCSdpParsingErrorInternal {
  required unsigned long lineNumber;
  required DOMString     error;
};

// This is for tracking the flow of SDP for about:webrtc
dictionary RTCSdpHistoryEntryInternal {
  required DOMHighResTimeStamp         timestamp;
  required boolean                     isLocal;
  required DOMString                   sdp;
  sequence<RTCSdpParsingErrorInternal> errors = [];
};

// This is intended to be a list of dictionaries that inherit from RTCStats
// (with some raw ICE candidates thrown in). Unfortunately, we cannot simply
// store a sequence<RTCStats> because of slicing. So, we have to have a
// separate list for each type. Used in c++ gecko code.
dictionary RTCStatsCollection {
  sequence<RTCInboundRtpStreamStats>        inboundRtpStreamStats = [];
  sequence<RTCOutboundRtpStreamStats>       outboundRtpStreamStats = [];
  sequence<RTCRemoteInboundRtpStreamStats>  remoteInboundRtpStreamStats = [];
  sequence<RTCRemoteOutboundRtpStreamStats> remoteOutboundRtpStreamStats = [];
  sequence<RTCMediaSourceStats>             mediaSourceStats = [];
  sequence<RTCVideoSourceStats>             videoSourceStats = [];
  sequence<RTCPeerConnectionStats>          peerConnectionStats = [];
  sequence<RTCRTPContributingSourceStats>   rtpContributingSourceStats = [];
  sequence<RTCIceCandidatePairStats>        iceCandidatePairStats = [];
  sequence<RTCIceCandidateStats>            iceCandidateStats = [];
  sequence<RTCIceCandidateStats>            trickledIceCandidateStats = [];
  sequence<RTCDataChannelStats>             dataChannelStats = [];
  sequence<RTCCodecStats>                   codecStats = [];
  sequence<RTCTransportStats>               transportStats = [];
  sequence<RTCCertificateStats>             certificateStats = [];

  // For internal use only
  sequence<DOMString>                       rawLocalCandidates = [];
  sequence<DOMString>                       rawRemoteCandidates = [];
  sequence<RTCVideoFrameHistoryInternal>    videoFrameHistories = [];
  sequence<RTCBandwidthEstimationInternal>  bandwidthEstimations = [];
};

// Details that about:webrtc can display about configured ICE servers
dictionary RTCIceServerInternal {
  sequence<DOMString> urls = [];
  required boolean    credentialProvided;
  required boolean    userNameProvided;
};

// Details that about:webrtc can display about the RTCConfiguration
// Chrome only
dictionary RTCConfigurationInternal {
  RTCBundlePolicy                bundlePolicy;
  required boolean               certificatesProvided;
  sequence<RTCIceServerInternal> iceServers = [];
  RTCIceTransportPolicy          iceTransportPolicy;
  required boolean               peerIdentityProvided;
  DOMString                      sdpSemantics;
  boolean alwaysNegotiateDataChannels = false;
};

dictionary RTCSdpHistoryInternal {
  required DOMString pcid;
  sequence<RTCSdpHistoryEntryInternal> sdpHistory = [];
};

// A collection of RTCStats dictionaries, plus some other info. Used by
// WebrtcGlobalInformation for about:webrtc, and telemetry.
dictionary RTCStatsReportInternal : RTCStatsCollection {
  required DOMString                        pcid;
  required unsigned long                    browserId;
  RTCConfigurationInternal                  configuration;
  DOMString                                 jsepSessionErrors;
  // TODO demux from RTCStatsReportInternal in bug 1830824
  sequence<RTCSdpHistoryEntryInternal>      sdpHistory = [];
  required DOMHighResTimeStamp              timestamp;
  double                                    callDurationMs;
  required unsigned long                    iceRestarts;
  required unsigned long                    iceRollbacks;
  boolean                                   offerer; // Is the PC the offerer
  required boolean                          closed; // Is the PC now closed
};

[Pref="media.peerconnection.enabled",
 Exposed=Window]
interface RTCStatsReport {

  // TODO(bug 1586109): Remove this once we no longer need to be able to
  // construct empty RTCStatsReports from JS.
  [ChromeOnly]
  constructor();

  readonly maplike<DOMString, object>;
};
