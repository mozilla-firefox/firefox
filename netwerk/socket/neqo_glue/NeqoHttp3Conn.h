/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NeqoHttp3Conn_h__
#define NeqoHttp3Conn_h__

#include <cstdint>
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
      const nsACString& aQlogDir, uint32_t aDatagramSize,
      uint32_t aProviderFlags, uint32_t aIdleTimeout, NeqoHttp3Conn** aConn) {
    return neqo_http3conn_new_use_nspr_for_io(
        &aOrigin, &aAlpn, &aLocalAddr, &aRemoteAddr, aMaxTableSize,
        aMaxBlockedStreams, aMaxData, aMaxStreamData, aVersionNegotiation,
        aWebTransport, &aQlogDir, aDatagramSize, aProviderFlags, aIdleTimeout,
        (const mozilla::net::NeqoHttp3Conn**)aConn);
  }

  static nsresult Init(const nsACString& aOrigin, const nsACString& aAlpn,
                       const NetAddr& aLocalAddr, const NetAddr& aRemoteAddr,
                       uint32_t aMaxTableSize, uint16_t aMaxBlockedStreams,
                       uint64_t aMaxData, uint64_t aMaxStreamData,
                       bool aVersionNegotiation, bool aWebTransport,
                       const nsACString& aQlogDir, uint32_t aDatagramSize,
                       uint32_t aProviderFlags, uint32_t aIdleTimeout,
                       int64_t socket, NeqoHttp3Conn** aConn) {
    return neqo_http3conn_new(
        &aOrigin, &aAlpn, &aLocalAddr, &aRemoteAddr, aMaxTableSize,
        aMaxBlockedStreams, aMaxData, aMaxStreamData, aVersionNegotiation,
        aWebTransport, &aQlogDir, aDatagramSize, aProviderFlags, aIdleTimeout,
        socket, (const mozilla::net::NeqoHttp3Conn**)aConn);
  }

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

  void SetResumptionToken(nsTArray<uint8_t>& aToken) {
    neqo_http3conn_set_resumption_token(this, &aToken);
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

  nsresult CloseWebTransport(uint64_t aSessionId, uint32_t aError,
                             const nsACString& aMessage) {
    return neqo_http3conn_webtransport_close_session(this, aSessionId, aError,
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
                                    uint64_t aTrackingId) {
    return neqo_http3conn_webtransport_send_datagram(this, aSessionId, &aData,
                                                     aTrackingId);
  }

  nsresult WebTransportMaxDatagramSize(uint64_t aSessionId, uint64_t* aResult) {
    return neqo_http3conn_webtransport_max_datagram_size(this, aSessionId,
                                                         aResult);
  }

  nsresult WebTransportSetSendOrder(uint64_t aSessionId,
                                    Maybe<int64_t> aSendOrder) {
    return neqo_http3conn_webtransport_set_sendorder(this, aSessionId,
                                                     aSendOrder.ptrOr(nullptr));
  }

 private:
  NeqoHttp3Conn() = delete;
  ~NeqoHttp3Conn() = delete;
  NeqoHttp3Conn(const NeqoHttp3Conn&) = delete;
  NeqoHttp3Conn& operator=(const NeqoHttp3Conn&) = delete;
};

class NeqoEncoder final {
 public:
  static void Init(NeqoEncoder** aEncoder) {
    neqo_encoder_new((const mozilla::net::NeqoEncoder**)aEncoder);
  }

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

 private:
  NeqoEncoder() = delete;
  ~NeqoEncoder() = delete;
  NeqoEncoder(const NeqoEncoder&) = delete;
  NeqoEncoder& operator=(const NeqoEncoder&) = delete;
};

class NeqoDecoder final {
 public:
  static void Init(const uint8_t* aBuf, uint32_t aCount,
                   NeqoDecoder** aDecoder) {
    neqo_decoder_new(aBuf, aCount, (const mozilla::net::NeqoDecoder**)aDecoder);
  }

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

 private:
  NeqoDecoder() = delete;
  ~NeqoDecoder() = delete;
  NeqoDecoder(const NeqoDecoder&) = delete;
  NeqoDecoder& operator=(const NeqoDecoder&) = delete;
};

}  // namespace net
}  // namespace mozilla

#endif
