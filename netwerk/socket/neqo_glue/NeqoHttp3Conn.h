/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NeqoHttp3Conn_h_
#define NeqoHttp3Conn_h_

#include <cstdint>

#include "mozilla/dom/PWebTransport.h"
#include "mozilla/net/neqo_glue_ffi_generated.h"

namespace mozilla {
namespace net {

class NeqoHttp3Conn final {
 public:
  static nsresult InitUseNSPRForIO(
      const nsACString& aOrigin, const nsACString& aAlpn,
      const NetAddr& aLocalAddr, const NetAddr& aRemoteAddr,
      uint32_t aMaxTableSize, uint16_t aMaxBlockedStreams, uint64_t aMaxData,
      uint64_t aMaxStreamData, bool aVersionNegotiation, bool aWebTransport,
      const nsACString& aQlogDir, uint32_t aIdleTimeout, uint32_t aFastPto,
      bool aPacingEnabled, NeqoHttp3Conn** aConn) {
    return neqo_http3conn_new_use_nspr_for_io(
        &aOrigin, &aAlpn, &aLocalAddr, &aRemoteAddr, aMaxTableSize,
        aMaxBlockedStreams, aMaxData, aMaxStreamData, aVersionNegotiation,
        aWebTransport, &aQlogDir, aIdleTimeout, aFastPto, aPacingEnabled,
        (const mozilla::net::NeqoHttp3Conn**)aConn);
  }

  static nsresult Init(const nsACString& aOrigin, const nsACString& aAlpn,
                       const NetAddr& aLocalAddr, const NetAddr& aRemoteAddr,
                       uint32_t aMaxTableSize, uint16_t aMaxBlockedStreams,
                       uint64_t aMaxData, uint64_t aMaxStreamData,
                       bool aVersionNegotiation, bool aWebTransport,
                       const nsACString& aQlogDir, uint32_t aIdleTimeout,
                       uint32_t aFastPto, int64_t socket, bool aPMTUDEnabled,
                       NeqoHttp3Conn** aConn) {
    return neqo_http3conn_new(
        &aOrigin, &aAlpn, &aLocalAddr, &aRemoteAddr, aMaxTableSize,
        aMaxBlockedStreams, aMaxData, aMaxStreamData, aVersionNegotiation,
        aWebTransport, &aQlogDir, aIdleTimeout, aFastPto, socket, aPMTUDEnabled,
        (const mozilla::net::NeqoHttp3Conn**)aConn);
  }

  NeqoHttp3Conn() = delete;
  ~NeqoHttp3Conn() = delete;
  NeqoHttp3Conn(const NeqoHttp3Conn&) = delete;
  NeqoHttp3Conn& operator=(const NeqoHttp3Conn&) = delete;

  void Close(uint64_t aError) { neqo_http3conn_close(this, aError); }

  nsresult GetSecInfo(NeqoSecretInfo* aSecInfo) {
    return neqo_http3conn_tls_info(this, aSecInfo);
  }

  nsresult PeerCertificateInfo(NeqoCertificateInfo* aCertInfo) {
    return neqo_http3conn_peer_certificate_info(this, aCertInfo);
  }

  void PeerAuthenticated(PRErrorCode aError) {
    neqo_http3conn_authenticated(this, aError);
  }

  nsresult ProcessInputUseNSPRForIO(const NetAddr& aRemoteAddr,
                                    const nsTArray<uint8_t>& aPacket) {
    return neqo_http3conn_process_input_use_nspr_for_io(this, &aRemoteAddr,
                                                        &aPacket);
  }

  ProcessInputResult ProcessInput() {
    return neqo_http3conn_process_input(this);
  }

  nsresult ProcessOutputAndSendUseNSPRForIO(void* aContext, SendFunc aSendFunc,
                                            SetTimerFunc aSetTimerFunc) {
    return neqo_http3conn_process_output_and_send_use_nspr_for_io(
        this, aContext, aSendFunc, aSetTimerFunc);
  }

  ProcessOutputAndSendResult ProcessOutputAndSend(void* aContext,
                                                  SetTimerFunc aSetTimerFunc) {
    return neqo_http3conn_process_output_and_send(this, aContext,
                                                  aSetTimerFunc);
  }

  nsresult GetEvent(Http3Event* aEvent, nsTArray<uint8_t>& aData) {
    return neqo_http3conn_event(this, aEvent, &aData);
  }

  nsresult Fetch(const nsACString& aMethod, const nsACString& aScheme,
                 const nsACString& aHost, const nsACString& aPath,
                 const nsACString& aHeaders, uint64_t* aStreamId,
                 uint8_t aUrgency, bool aIncremental) {
    return neqo_http3conn_fetch(this, &aMethod, &aScheme, &aHost, &aPath,
                                &aHeaders, aStreamId, aUrgency, aIncremental);
  }

  nsresult Connect(const nsACString& aHost, const nsACString& aHeaders,
                   uint64_t* aStreamId, uint8_t aUrgency, bool aIncremental) {
    return neqo_http3conn_connect(this, &aHost, &aHeaders, aStreamId, aUrgency,
                                  aIncremental);
  }

  nsresult PriorityUpdate(uint64_t aStreamId, uint8_t aUrgency,
                          bool aIncremental) {
    return neqo_http3conn_priority_update(this, aStreamId, aUrgency,
                                          aIncremental);
  }

  nsresult SendRequestBody(uint64_t aStreamId, const uint8_t* aBuf,
                           uint32_t aCount, uint32_t* aCountRead) {
    return neqo_htttp3conn_send_request_body(this, aStreamId, aBuf, aCount,
                                             aCountRead);
  }

  // This closes only the sending side of a stream.
  nsresult CloseStream(uint64_t aStreamId) {
    return neqo_http3conn_close_stream(this, aStreamId);
  }

  nsresult ReadResponseData(uint64_t aStreamId, uint8_t* aBuf, uint32_t aLen,
                            uint32_t* aRead, bool* aFin) {
    return neqo_http3conn_read_response_data(this, aStreamId, aBuf, aLen, aRead,
                                             aFin);
  }

  void CancelFetch(uint64_t aStreamId, uint64_t aError) {
    neqo_http3conn_cancel_fetch(this, aStreamId, aError);
  }

  void ResetStream(uint64_t aStreamId, uint64_t aError) {
    neqo_http3conn_reset_stream(this, aStreamId, aError);
  }

  void StreamStopSending(uint64_t aStreamId, uint64_t aError) {
    neqo_http3conn_stream_stop_sending(this, aStreamId, aError);
  }

  nsresult SetResumptionToken(nsTArray<uint8_t>& aToken) {
    return neqo_http3conn_set_resumption_token(this, &aToken);
  }

  void SetEchConfig(nsTArray<uint8_t>& aEchConfig) {
    neqo_http3conn_set_ech_config(this, &aEchConfig);
  }

  bool IsZeroRtt() { return neqo_http3conn_is_zero_rtt(this); }

  void AddRef() { neqo_http3conn_addref(this); }
  void Release() { neqo_http3conn_release(this); }

  void GetStats(Http3Stats* aStats) {
    return neqo_http3conn_get_stats(this, aStats);
  }

  nsresult CreateWebTransport(const nsACString& aHost, const nsACString& aPath,
                              const nsACString& aHeaders,
                              uint64_t* aSessionId) {
    return neqo_http3conn_webtransport_create_session(this, &aHost, &aPath,
                                                      &aHeaders, aSessionId);
  }

  nsresult CreateConnectUdp(const nsACString& aHost, const nsACString& aPath,
                            const nsACString& aHeaders, uint64_t* aSessionId) {
    return neqo_http3conn_connect_udp_create_session(this, &aHost, &aPath,
                                                     &aHeaders, aSessionId);
  }

  bool CloseWebTransport(uint64_t aSessionId, uint32_t aError,
                         const nsACString& aMessage,
                         mozilla::dom::WebTransportStatsData& aStats) {
    WebTransportSessionStats stats{};
    nsresult rv = neqo_http3conn_webtransport_close_session(
        this, aSessionId, aError, &aMessage, &stats);
    if (NS_FAILED(rv)) {
      return false;
    }
    TranslateWebTransportSessionStats(stats, aStats);
    return true;
  }

  nsresult CloseConnectUdp(uint64_t aSessionId, uint32_t aError,
                           const nsACString& aMessage) {
    return neqo_http3conn_connect_udp_close_session(this, aSessionId, aError,
                                                    &aMessage);
  }

  nsresult CreateWebTransportStream(uint64_t aSessionId,
                                    WebTransportStreamType aStreamType,
                                    uint64_t* aStreamId) {
    return neqo_http3conn_webtransport_create_stream(this, aSessionId,
                                                     aStreamType, aStreamId);
  }

  nsresult WebTransportSendDatagram(uint64_t aSessionId,
                                    nsTArray<uint8_t>& aData,
                                    uint64_t aTrackingId,
                                    bool* aBelowHighWaterMark,
                                    uint64_t aSendGroupId, int64_t aSendOrder,
                                    uint64_t aAgeUs) {
    return neqo_http3conn_webtransport_send_datagram(
        this, aSessionId, &aData, aTrackingId, aBelowHighWaterMark,
        aSendGroupId, aSendOrder, aAgeUs);
  }

  nsresult ConnectUdpSendDatagram(uint64_t aSessionId, nsTArray<uint8_t>& aData,
                                  uint64_t aTrackingId, uint64_t aSendGroupId,
                                  int64_t aSendOrder) {
    return neqo_http3conn_connect_udp_send_datagram(
        this, aSessionId, &aData, aTrackingId, aSendGroupId, aSendOrder);
  }

  nsresult WebTransportMaxDatagramSize(uint64_t aSessionId, uint64_t* aResult) {
    return neqo_http3conn_webtransport_max_datagram_size(this, aSessionId,
                                                         aResult);
  }

  nsresult WebTransportSetDatagramMaxBufferedDatagrams(
      uint64_t aSessionId, uint32_t aMaxBufferedDatagrams) {
    return neqo_http3conn_webtransport_set_datagram_max_buffered_datagrams(
        this, aSessionId, aMaxBufferedDatagrams);
  }

  nsresult WebTransportSetDatagramMaxAge(uint64_t aSessionId, double aMaxAge) {
    return neqo_http3conn_webtransport_set_datagram_max_age(this, aSessionId,
                                                            aMaxAge);
  }

  bool GetWebTransportSessionStats(
      uint64_t aSessionId, mozilla::dom::WebTransportStatsData& aStats) {
    WebTransportSessionStats stats{};
    nsresult rv =
        neqo_http3conn_webtransport_session_stats(this, aSessionId, &stats);
    if (NS_FAILED(rv)) {
      return false;
    }
    TranslateWebTransportSessionStats(stats, aStats);
    return true;
  }

  // Only the connection-level counters; used once neqo has dropped the
  // session, which leaves the session-scoped datagram counters at 0.
  void GetWebTransportTransportStats(
      mozilla::dom::WebTransportStatsData& aStats) {
    struct WebTransportSessionStats stats = {};
    neqo_http3conn_webtransport_transport_stats(this, &stats);
    TranslateWebTransportSessionStats(stats, aStats);
  }

  nsresult WebTransportSetSendOrder(uint64_t aSessionId, int64_t aSendOrder) {
    return neqo_http3conn_webtransport_set_sendorder(this, aSessionId,
                                                     &aSendOrder);
  }

  nsresult WebTransportSetSendGroup(uint64_t aSessionId,
                                    uint64_t aSendGroupId) {
    return neqo_http3conn_webtransport_set_sendgroup(this, aSessionId,
                                                     aSendGroupId);
  }

  nsresult RegisterWebTransportSendGroup(uint64_t aSessionId,
                                         uint64_t aGroupId) {
    return neqo_http3conn_webtransport_register_send_group(this, aSessionId,
                                                           aGroupId);
  }
  nsresult SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) {
    return neqo_http3conn_webtransport_set_anticipated_concurrent_incoming_uni_streams(
        this, aSessionId, aValue);
  }

  nsresult SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) {
    return neqo_http3conn_webtransport_set_anticipated_concurrent_incoming_bidi_streams(
        this, aSessionId, aValue);
  }
  nsresult GetWebTransportSessionProtocol(uint64_t aSessionId,
                                          nsACString& aProtocol) {
    return neqo_http3conn_webtransport_session_protocol(this, aSessionId,
                                                        &aProtocol);
  }

  nsresult ExportWebTransportKeyingMaterial(
      uint64_t aSessionId, const nsTArray<uint8_t>& aLabel,
      const nsTArray<uint8_t>& aContext, uint32_t aOutputLength,
      nsTArray<uint8_t>& aKeyingMaterial) {
    aKeyingMaterial.SetLength(aOutputLength);
    return neqo_http3conn_export_keying_material(
        this, aSessionId, aLabel.Elements(), aLabel.Length(),
        aContext.Elements(), aContext.Length(), aKeyingMaterial.Elements(),
        aOutputLength);
  }

 private:
  static void TranslateWebTransportSessionStats(
      const struct WebTransportSessionStats& aFrom,
      mozilla::dom::WebTransportStatsData& aTo) {
    // Use transport-level totals for spec compliance
    aTo.bytesSent() = aFrom.bytes_sent_total;
    // bytesSentOverhead is omitted: we don't separate application vs
    // protocol overhead. See bug 2051624.
    aTo.bytesAcknowledged() = aFrom.bytes_acked;
    aTo.packetsSent() = aFrom.packets_sent;
    aTo.bytesLost() = aFrom.bytes_lost;
    aTo.packetsLost() = aFrom.packets_lost;
    aTo.bytesReceived() = aFrom.bytes_received_total;
    aTo.packetsReceived() = aFrom.packets_received;
    aTo.smoothedRtt() = aFrom.smoothed_rtt;
    aTo.rttVariation() = aFrom.rtt_variation;
    aTo.minRtt() = aFrom.min_rtt;
    aTo.estimatedSendRate() = aFrom.estimated_send_rate;
    aTo.atSendCapacity() = aFrom.at_send_capacity;
    // neqo doesn't track droppedIncoming/expiredIncoming per-session yet;
    // report 0 until it does. lostOutgoing uses the connection-level counter
    // as a stand-in, since Firefox doesn't support WebTransport connection
    // pooling.
    aTo.datagrams().droppedIncoming() = 0;
    aTo.datagrams().expiredIncoming() = 0;
    aTo.datagrams().expiredOutgoing() = aFrom.datagrams_expired_outgoing;
    aTo.datagrams().lostOutgoing() = aFrom.datagrams_lost_outgoing;
  }
};

class NeqoEncoder final {
 public:
  static void Init(NeqoEncoder** aEncoder) {
    neqo_encoder_new((const mozilla::net::NeqoEncoder**)aEncoder);
  }
  NeqoEncoder() = delete;
  ~NeqoEncoder() = delete;
  NeqoEncoder(const NeqoEncoder&) = delete;
  NeqoEncoder& operator=(const NeqoEncoder&) = delete;

  void EncodeByte(uint8_t aData) { neqo_encode_byte(this, aData); }

  void EncodeVarint(uint64_t aData) { neqo_encode_varint(this, aData); }

  void EncodeUint(uint32_t aSize, uint64_t aData) {
    neqo_encode_uint(this, aSize, aData);
  }

  void EncodeBuffer(const uint8_t* aBuf, uint32_t aCount) {
    neqo_encode_buffer(this, aBuf, aCount);
  }

  void EncodeBufferWithVarintLen(const uint8_t* aBuf, uint32_t aCount) {
    neqo_encode_vvec(this, aBuf, aCount);
  }

  void GetData(const uint8_t** aBuf, uint32_t* aLength) {
    return neqo_encode_get_data(this, aBuf, aLength);
  }

  static size_t VarintLength(uint64_t aValue) {
    return neqo_encode_varint_len(aValue);
  }

  void AddRef() { neqo_encoder_addref(this); }
  void Release() { neqo_encoder_release(this); }
};

class NeqoDecoder final {
 public:
  static void Init(const uint8_t* aBuf, uint32_t aCount,
                   NeqoDecoder** aDecoder) {
    neqo_decoder_new(aBuf, aCount, (const mozilla::net::NeqoDecoder**)aDecoder);
  }
  NeqoDecoder() = delete;
  ~NeqoDecoder() = delete;
  NeqoDecoder(const NeqoDecoder&) = delete;
  NeqoDecoder& operator=(const NeqoDecoder&) = delete;

  bool DecodeVarint(uint64_t* aResult) {
    return neqo_decode_varint(this, aResult);
  }

  bool DecodeUint32(uint32_t* aResult) {
    return neqo_decode_uint32(this, aResult);
  }

  bool Decode(uint32_t aCount, const uint8_t** aBuf, uint32_t* aLength) {
    return neqo_decode(this, aCount, aBuf, aLength);
  }

  void DecodeRemainder(const uint8_t** aBuf, uint32_t* aLength) {
    neqo_decode_remainder(this, aBuf, aLength);
  }

  uint64_t Remaining() { return neqo_decoder_remaining(this); }

  uint64_t Offset() { return neqo_decoder_offset(this); }

  void AddRef() { neqo_decoder_addref(this); }
  void Release() { neqo_decoder_release(this); }
};

}  // namespace net
}  // namespace mozilla

#endif
