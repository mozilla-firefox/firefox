/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_SRTP_SESSION_H_
#define PC_SRTP_SESSION_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "api/field_trials_view.h"
#include "api/rtp_header_extension_id.h"
#include "api/sequence_checker.h"
#include "rtc_base/buffer.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"

// Forward declaration to avoid pulling in libsrtp headers here
struct srtp_event_data_t;
struct srtp_ctx_t_;  // Trailing _ is required.

namespace webrtc {

// Prohibits webrtc from initializing libsrtp. This can be used if libsrtp is
// initialized by another library or explicitly. Note that this must be called
// before creating an SRTP session with WebRTC.
void ProhibitLibsrtpInitialization();

// Class that wraps a libSRTP session.
class SrtpSession {
 public:
  SrtpSession();
  explicit SrtpSession(const FieldTrialsView& field_trials);
  ~SrtpSession();

  SrtpSession(const SrtpSession&) = delete;
  SrtpSession& operator=(const SrtpSession&) = delete;

  // Enables use of cryptex, may update existing session. Calling this on a
  // receiving session enables receiving cryptex unconditionally.
  bool UseCryptex(bool negotiated, bool required, bool sending_session);

  // Configures the session for sending data using the specified
  // crypto suite and key. Receiving must be done by a separate session.
  bool SetSend(int crypto_suite,
               const ZeroOnFreeBuffer<uint8_t>& key,
               const std::vector<RtpHeaderExtensionId>& extension_ids);
  bool UpdateSend(int crypto_suite,
                  const ZeroOnFreeBuffer<uint8_t>& key,
                  const std::vector<RtpHeaderExtensionId>& extension_ids);

  // Configures the session for receiving data using the specified
  // crypto suite and key. Sending must be done by a separate session.
  bool SetReceive(int crypto_suite,
                  const ZeroOnFreeBuffer<uint8_t>& key,
                  const std::vector<RtpHeaderExtensionId>& extension_ids);
  bool UpdateReceive(int crypto_suite,
                     const ZeroOnFreeBuffer<uint8_t>& key,
                     const std::vector<RtpHeaderExtensionId>& extension_ids);

  // Encrypts/signs an individual RTP/RTCP packet, in-place.
  // If an HMAC is used, this will increase the packet size.
  bool ProtectRtp(CopyOnWriteBuffer& buffer, int64_t* index);
  bool ProtectRtp(CopyOnWriteBuffer& buffer);

  bool ProtectRtcp(CopyOnWriteBuffer& buffer);
  // Decrypts/verifies an invidiual RTP/RTCP packet.
  // If an HMAC is used, this will decrease the packet size.
  bool UnprotectRtp(CopyOnWriteBuffer& buffer);
  bool UnprotectRtcp(CopyOnWriteBuffer& buffer);

  int GetSrtpOverhead() const;

  // Removes a SSRC from the underlying libSRTP session.
  // Note: this should only be done for SSRCs that are received.
  // Removing SSRCs that were sent and then reusing them leads to
  // cryptographic weaknesses described in
  // https://www.rfc-editor.org/rfc/rfc3711#section-8
  // https://www.rfc-editor.org/rfc/rfc7714#section-8.4
  bool RemoveSsrcFromSession(uint32_t ssrc);

 private:
  bool DoSetKey(int type,
                int crypto_suite,
                const ZeroOnFreeBuffer<uint8_t>& key,
                const std::vector<RtpHeaderExtensionId>& extension_ids);
  bool SetKey(int type,
              int crypto_suite,
              const ZeroOnFreeBuffer<uint8_t>& key,
              const std::vector<RtpHeaderExtensionId>& extension_ids);
  bool UpdateKey(int type,
                 int crypto_suite,
                 const ZeroOnFreeBuffer<uint8_t>& key,
                 const std::vector<RtpHeaderExtensionId>& extension_ids);
  // Returns send stream current packet index from srtp db.
  bool GetSendStreamPacketIndex(CopyOnWriteBuffer& buffer, int64_t* index);

  // Writes unencrypted packets in text2pcap format to the log file
  // for debugging.
  void DumpPacket(const CopyOnWriteBuffer& buffer, bool outbound);

  void HandleEvent(const srtp_event_data_t* ev);
  static void HandleEventThunk(srtp_event_data_t* ev);

  RTC_NO_UNIQUE_ADDRESS SequenceChecker thread_checker_{
      SequenceChecker::kDetached};
  srtp_ctx_t_* session_ RTC_GUARDED_BY(thread_checker_) = nullptr;

  // Overhead of the SRTP auth tag for RTP and RTCP in bytes.
  // Depends on the cipher suite used and is usually the same with the exception
  // of the kCsAesCm128HmacSha1_32 cipher suite. The additional four bytes
  // required for RTCP protection are not included.
  int rtp_auth_tag_len_ RTC_GUARDED_BY(thread_checker_) = 0;
  int rtcp_auth_tag_len_ RTC_GUARDED_BY(thread_checker_) = 0;

  bool inited_ RTC_GUARDED_BY(thread_checker_) = false;
  int last_send_seq_num_ RTC_GUARDED_BY(thread_checker_) = -1;
  int decryption_failure_count_ RTC_GUARDED_BY(thread_checker_) = 0;
  int rtcp_decryption_failure_count_ RTC_GUARDED_BY(thread_checker_) = 0;

  // Supported since libsrtp v2.8.0.
  bool use_cryptex_ RTC_GUARDED_BY(thread_checker_) = false;
  bool require_cryptex_ RTC_GUARDED_BY(thread_checker_) = false;

  const bool dump_plain_rtp_ = false;
};

}  //  namespace webrtc

#endif  // PC_SRTP_SESSION_H_
