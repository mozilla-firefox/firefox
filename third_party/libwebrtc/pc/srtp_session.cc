/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/srtp_session.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/field_trials_view.h"
#include "api/rtp_header_extension_id.h"
#include "api/sequence_checker.h"
#include "modules/rtp_rtcp/source/rtp_util.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/text2pcap.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/metrics.h"
#include "third_party/libsrtp/include/srtp.h"

#ifndef SRTP_SRCTP_INDEX_LEN
#define SRTP_SRCTP_INDEX_LEN 4
#endif

namespace webrtc {

constexpr uint16_t kRtpOneByteHeaderExtensionProfile = 0xBEDE;
constexpr uint16_t kRtpTwoByteHeaderExtensionProfile = 0x1000;

namespace {
class LibSrtpInitializer {
 public:
  // Returns singleton instance of this class. Instance created on first use,
  // and never destroyed.
  static LibSrtpInitializer& Get() {
    static LibSrtpInitializer* const instance = new LibSrtpInitializer();
    return *instance;
  }

  // There is only one global log handler in libsrtp so we can not resolve this
  // to a particular session.
  static void LibSrtpLogHandler(srtp_log_level_t level,
                                const char* msg,
                                void* data);
  void ProhibitLibsrtpInitialization();

  // These methods are responsible for initializing libsrtp (if the usage count
  // is incremented from 0 to 1) or deinitializing it (when decremented from 1
  // to 0).
  //
  // Returns true if successful (will always be successful if already inited).
  bool IncrementLibsrtpUsageCountAndMaybeInit(
      srtp_event_handler_func_t* event_handler);
  void DecrementLibsrtpUsageCountAndMaybeDeinit();

 private:
  LibSrtpInitializer() = default;

  Mutex mutex_;
  int usage_count_ RTC_GUARDED_BY(mutex_) = 0;
};

void LibSrtpInitializer::LibSrtpLogHandler(srtp_log_level_t level,
                                           const char* msg,
                                           void* data) {
  RTC_DCHECK(data == nullptr);
  if (level == srtp_log_level_error) {
    RTC_LOG(LS_ERROR) << "SRTP log: " << msg;
  } else if (level == srtp_log_level_warning) {
    RTC_LOG(LS_WARNING) << "SRTP log: " << msg;
  } else if (level == srtp_log_level_info) {
    RTC_LOG(LS_INFO) << "SRTP log: " << msg;
  } else if (level == srtp_log_level_debug) {
    RTC_LOG(LS_VERBOSE) << "SRTP log: " << msg;
  }
}

void LibSrtpInitializer::ProhibitLibsrtpInitialization() {
  MutexLock lock(&mutex_);
  ++usage_count_;
}

bool LibSrtpInitializer::IncrementLibsrtpUsageCountAndMaybeInit(
    srtp_event_handler_func_t* event_handler) {
  MutexLock lock(&mutex_);
  RTC_DCHECK(event_handler);

  RTC_DCHECK_GE(usage_count_, 0);
  if (usage_count_ == 0) {
    int err;

    err = srtp_install_log_handler(&LibSrtpInitializer::LibSrtpLogHandler,
                                   nullptr);
    if (err != srtp_err_status_ok) {
      RTC_LOG(LS_ERROR) << "Failed to install libsrtp log handler, err=" << err;
      return false;
    }
    err = srtp_init();
    if (err != srtp_err_status_ok) {
      RTC_LOG(LS_ERROR) << "Failed to init SRTP, err=" << err;
      return false;
    }

    err = srtp_install_event_handler(event_handler);
    if (err != srtp_err_status_ok) {
      RTC_LOG(LS_ERROR) << "Failed to install SRTP event handler, err=" << err;
      return false;
    }
  }
  ++usage_count_;
  return true;
}

void LibSrtpInitializer::DecrementLibsrtpUsageCountAndMaybeDeinit() {
  MutexLock lock(&mutex_);

  RTC_DCHECK_GE(usage_count_, 1);
  if (--usage_count_ == 0) {
    int err = srtp_install_log_handler(nullptr, nullptr);
    if (err != srtp_err_status_ok) {
      RTC_LOG(LS_ERROR) << "Failed to uninstall libsrtp log handler, err="
                        << err;
    }
    err = srtp_shutdown();
    if (err != srtp_err_status_ok) {
      RTC_LOG(LS_ERROR) << "srtp_shutdown failed. err=" << err;
    }
  }
}

}  // namespace

// One more than the maximum libsrtp error code. Required by
// RTC_HISTOGRAM_ENUMERATION. Keep this in sync with srtp_error_status_t defined
// in srtp.h.
constexpr int kSrtpErrorCodeBoundary = 29;

SrtpSession::SrtpSession() {}

SrtpSession::SrtpSession(const FieldTrialsView& field_trials)
    : dump_plain_rtp_(field_trials.IsEnabled("WebRTC-Debugging-RtpDump")) {}

SrtpSession::~SrtpSession() {
  if (session_) {
    srtp_set_user_data(session_, nullptr);
    srtp_dealloc(session_);
  }
  if (inited_) {
    LibSrtpInitializer::Get().DecrementLibsrtpUsageCountAndMaybeDeinit();
  }
}

bool SrtpSession::UseCryptex(bool negotiated,
                             bool required,
                             bool sending_session) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (session_) {
    srtp_ssrc_t ssrc;
    ssrc.type = sending_session ? ssrc_any_outbound : ssrc_any_inbound;
    int err = srtp_set_stream_use_cryptex(session_, &ssrc,
                                          negotiated || !sending_session);
    // Should not fail given the usage in libWebRTC.
    RTC_DCHECK(err == srtp_err_status_ok);
    if (err != srtp_err_status_ok) {
      return false;
    }
  }
  use_cryptex_ = negotiated;
  require_cryptex_ = required;
  return true;
}

bool SrtpSession::SetSend(
    int crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& key,
    const std::vector<RtpHeaderExtensionId>& extension_ids) {
  return SetKey(ssrc_any_outbound, crypto_suite, key, extension_ids);
}

bool SrtpSession::UpdateSend(
    int crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& key,
    const std::vector<RtpHeaderExtensionId>& extension_ids) {
  return UpdateKey(ssrc_any_outbound, crypto_suite, key, extension_ids);
}

bool SrtpSession::SetReceive(
    int crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& key,
    const std::vector<RtpHeaderExtensionId>& extension_ids) {
  return SetKey(ssrc_any_inbound, crypto_suite, key, extension_ids);
}

bool SrtpSession::UpdateReceive(
    int crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& key,
    const std::vector<RtpHeaderExtensionId>& extension_ids) {
  return UpdateKey(ssrc_any_inbound, crypto_suite, key, extension_ids);
}

bool SrtpSession::ProtectRtp(CopyOnWriteBuffer& buffer) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!session_) {
    RTC_LOG(LS_WARNING) << "Failed to protect SRTP packet: no SRTP Session";
    return false;
  }

  // Note: the need_len differs from the libsrtp recommendation to ensure
  // SRTP_MAX_TRAILER_LEN bytes of free space after the data. WebRTC
  // never includes a MKI, therefore the amount of bytes added by the
  // srtp_protect call is known in advance and depends on the cipher suite.
  size_t need_len = buffer.size() + rtp_auth_tag_len_;  // NOLINT
  if (buffer.capacity() < need_len) {
    RTC_LOG(LS_WARNING) << "Failed to protect SRTP packet: The buffer length "
                        << buffer.capacity() << " is less than the needed "
                        << need_len;
    return false;
  }
  if (dump_plain_rtp_) {
    DumpPacket(buffer, /*outbound=*/true);
  }

  int out_len = buffer.size();
  int err = srtp_protect(session_, buffer.MutableData<char>(), &out_len);
  int seq_num = ParseRtpSequenceNumber(buffer);
  if (err != srtp_err_status_ok) {
    RTC_LOG(LS_WARNING) << "Failed to protect SRTP packet, seqnum=" << seq_num
                        << ", err=" << err
                        << ", last seqnum=" << last_send_seq_num_;
    return false;
  }
  buffer.SetSize(out_len);
  last_send_seq_num_ = seq_num;
  return true;
}

bool SrtpSession::ProtectRtp(CopyOnWriteBuffer& buffer, int64_t* index) {
  if (!ProtectRtp(buffer)) {
    return false;
  }
  return (index) ? GetSendStreamPacketIndex(buffer, index) : true;
}

bool SrtpSession::ProtectRtcp(CopyOnWriteBuffer& buffer) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!session_) {
    RTC_LOG(LS_WARNING) << "Failed to protect SRTCP packet: no SRTP Session";
    return false;
  }

  // Note: the need_len differs from the libsrtp recommendation to ensure
  // SRTP_MAX_TRAILER_LEN bytes of free space after the data. WebRTC
  // never includes a MKI, therefore the amount of bytes added by the
  // srtp_protect_rtp call is known in advance and depends on the cipher suite.
  size_t need_len =
      buffer.size() + sizeof(uint32_t) + rtcp_auth_tag_len_;  // NOLINT
  if (buffer.capacity() < need_len) {
    RTC_LOG(LS_WARNING)
        << "Failed to protect SRTCP packet: The buffer capacity "
        << buffer.capacity() << " is less than the needed " << need_len;
    return false;
  }
  if (dump_plain_rtp_) {
    DumpPacket(buffer, /*outbound=*/true);
  }

  int out_len = buffer.size();
  int err = srtp_protect_rtcp(session_, buffer.MutableData<char>(), &out_len);
  if (err != srtp_err_status_ok) {
    RTC_LOG(LS_WARNING) << "Failed to protect SRTCP packet, err=" << err;
    return false;
  }
  buffer.SetSize(out_len);
  return true;
}

bool SrtpSession::UnprotectRtp(CopyOnWriteBuffer& buffer) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!session_) {
    RTC_LOG(LS_WARNING) << "Failed to unprotect SRTP packet: no SRTP Session";
    return false;
  }
  int out_len = buffer.size();

  std::optional<uint16_t> extension_profile;
  if (require_cryptex_) {
    // After decryption this will be set to 0xBEDE or 0x1000 for one/two byte
    // extensions so must be parsed before decryption. If no extensions are
    // present this returns std::nullopt.
    extension_profile = ParseRtpExtensionProfile(buffer);
  }
  int err = srtp_unprotect(session_, buffer.MutableData<char>(), &out_len);
  if (err == srtp_err_status_ok && require_cryptex_ &&
      (extension_profile == kRtpOneByteHeaderExtensionProfile ||
       extension_profile == kRtpTwoByteHeaderExtensionProfile)) {
    // An additional check whether cryptex is used when required.
    // TODO: bugs.webrtc.org/455813732 - libSRTP will be doing this check
    // after https://github.com/cisco/libsrtp/pull/805
    err = srtp_err_status_cryptex_err;
  }
  if (err != srtp_err_status_ok) {
    // Limit the error logging to avoid excessive logs when there are lots of
    // bad packets.
    const int kFailureLogThrottleCount = 100;
    if (decryption_failure_count_ % kFailureLogThrottleCount == 0) {
      RTC_LOG(LS_WARNING) << "Failed to unprotect SRTP packet, err=" << err
                          << ", previous failure count: "
                          << decryption_failure_count_;
    }
    ++decryption_failure_count_;
    RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.SrtpUnprotectError",
                              static_cast<int>(err), kSrtpErrorCodeBoundary);
    return false;
  }
  buffer.SetSize(out_len);
  if (dump_plain_rtp_) {
    DumpPacket(buffer, /*outbound=*/false);
  }
  return true;
}

bool SrtpSession::UnprotectRtcp(CopyOnWriteBuffer& buffer) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!session_) {
    RTC_LOG(LS_WARNING) << "Failed to unprotect SRTCP packet: no SRTP Session";
    return false;
  }

  int out_len = buffer.size();
  int err = srtp_unprotect_rtcp(session_, buffer.MutableData<char>(), &out_len);
  if (err != srtp_err_status_ok) {
    // Limit the error logging to avoid excessive logs when there are lots of
    // bad packets.
    const int kFailureLogThrottleCount = 100;
    if (rtcp_decryption_failure_count_ % kFailureLogThrottleCount == 0) {
      RTC_LOG(LS_WARNING) << "Failed to unprotect SRTCP packet, err=" << err
                          << ", previous failure count: "
                          << rtcp_decryption_failure_count_;
    }
    ++rtcp_decryption_failure_count_;
    RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.SrtcpUnprotectError",
                              static_cast<int>(err), kSrtpErrorCodeBoundary);
    return false;
  }
  buffer.SetSize(out_len);
  if (dump_plain_rtp_) {
    DumpPacket(buffer, /*outbound=*/false);
  }
  return true;
}

int SrtpSession::GetSrtpOverhead() const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rtp_auth_tag_len_;
}

bool SrtpSession::RemoveSsrcFromSession(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(session_);
  // libSRTP expects the SSRC to be in network byte order.
  return srtp_remove_stream(session_, htonl(ssrc)) == srtp_err_status_ok;
}

bool SrtpSession::GetSendStreamPacketIndex(CopyOnWriteBuffer& buffer,
                                           int64_t* index) {
  RTC_DCHECK_RUN_ON(&thread_checker_);

  uint32_t ssrc = ParseRtpSsrc(buffer);
  uint32_t roc;
  if (srtp_get_stream_roc(session_, ssrc, &roc) != srtp_err_status_ok) {
    return false;
  }
  // Calculate the extended sequence number.
  uint16_t seq_num = ParseRtpSequenceNumber(buffer);
  int64_t extended_seq_num = (roc << 16) + seq_num;

  // Shift extended sequence number, put into network byte order
  *index = static_cast<int64_t>(NetworkToHost64(extended_seq_num << 16));
  return true;
}

bool SrtpSession::DoSetKey(
    int type,
    int crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& key,
    const std::vector<RtpHeaderExtensionId>& extension_ids) {
  RTC_DCHECK_RUN_ON(&thread_checker_);

  srtp_policy_t policy;
  memset(&policy, 0, sizeof(policy));
  if (!(srtp_crypto_policy_set_from_profile_for_rtp(
            &policy.rtp, (srtp_profile_t)crypto_suite) == srtp_err_status_ok &&
        srtp_crypto_policy_set_from_profile_for_rtcp(
            &policy.rtcp, (srtp_profile_t)crypto_suite) ==
            srtp_err_status_ok)) {
    RTC_LOG(LS_ERROR) << "Failed to " << (session_ ? "update" : "create")
                      << " SRTP session: unsupported cipher_suite "
                      << crypto_suite;
    return false;
  }

  if (key.size() != static_cast<size_t>(policy.rtp.cipher_key_len)) {
    RTC_LOG(LS_ERROR) << "Failed to " << (session_ ? "update" : "create")
                      << " SRTP session: invalid key";
    return false;
  }

  policy.ssrc.type = static_cast<srtp_ssrc_type_t>(type);
  policy.ssrc.value = 0;
  policy.key = const_cast<uint8_t*>(key.data());
  // TODO(astor) parse window size from WSH session-param
  policy.window_size = 1024;
  policy.allow_repeat_tx = 1;
  // If both encrypted extension ids and cryptex are in use,
  // cryptex takes precedence and encrypted extensions remain
  // empty for libSRTP.
  std::vector<int> extension_ids_int;
  if (!extension_ids.empty() && !use_cryptex_) {
    extension_ids_int.reserve(extension_ids.size());
    for (RtpHeaderExtensionId id : extension_ids) {
      extension_ids_int.push_back(id.value());
    }
    policy.enc_xtn_hdr = &extension_ids_int[0];
    policy.enc_xtn_hdr_count = static_cast<int>(extension_ids_int.size());
  }
  policy.next = nullptr;

  if (!session_) {
    int err = srtp_create(&session_, &policy);
    if (err != srtp_err_status_ok) {
      session_ = nullptr;
      RTC_LOG(LS_ERROR) << "Failed to create SRTP session, err=" << err;
      return false;
    }
    srtp_set_user_data(session_, this);
  } else {
    int err = srtp_update(session_, &policy);
    if (err != srtp_err_status_ok) {
      RTC_LOG(LS_ERROR) << "Failed to update SRTP session, err=" << err;
      return false;
    }
  }
  int err = srtp_set_stream_use_cryptex(session_, &policy.ssrc, use_cryptex_);
  if (err != srtp_err_status_ok) {
    RTC_LOG(LS_ERROR) << "Failed to update SRTP session cryptex, err=" << err;
    return false;
  }

  rtp_auth_tag_len_ = policy.rtp.auth_tag_len;
  rtcp_auth_tag_len_ = policy.rtcp.auth_tag_len;
  return true;
}

bool SrtpSession::SetKey(
    int type,
    int crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& key,
    const std::vector<RtpHeaderExtensionId>& extension_ids) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (session_) {
    RTC_LOG(LS_ERROR) << "Failed to create SRTP session: "
                         "SRTP session already created";
    return false;
  }

  // This is the first time we need to actually interact with libsrtp, so
  // initialize it if needed.
  if (LibSrtpInitializer::Get().IncrementLibsrtpUsageCountAndMaybeInit(
          &SrtpSession::HandleEventThunk)) {
    inited_ = true;
  } else {
    return false;
  }

  return DoSetKey(type, crypto_suite, key, extension_ids);
}

bool SrtpSession::UpdateKey(
    int type,
    int crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& key,
    const std::vector<RtpHeaderExtensionId>& extension_ids) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!session_) {
    RTC_LOG(LS_ERROR) << "Failed to update non-existing SRTP session";
    return false;
  }

  return DoSetKey(type, crypto_suite, key, extension_ids);
}

void ProhibitLibsrtpInitialization() {
  LibSrtpInitializer::Get().ProhibitLibsrtpInitialization();
}

void SrtpSession::HandleEvent(const srtp_event_data_t* ev) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  switch (ev->event) {
    case event_ssrc_collision:
      RTC_LOG(LS_INFO) << "SRTP event: SSRC collision";
      break;
    case event_key_soft_limit:
      RTC_LOG(LS_INFO) << "SRTP event: reached soft key usage limit";
      break;
    case event_key_hard_limit:
      RTC_LOG(LS_INFO) << "SRTP event: reached hard key usage limit";
      break;
    case event_packet_index_limit:
      RTC_LOG(LS_INFO)
          << "SRTP event: reached hard packet limit (2^48 packets)";
      break;
    default:
      RTC_LOG(LS_INFO) << "SRTP event: unknown " << ev->event;
      break;
  }
}

void SrtpSession::HandleEventThunk(srtp_event_data_t* ev) {
  // Callback will be executed from same thread that calls the "srtp_protect"
  // and "srtp_unprotect" functions.
  SrtpSession* session =
      static_cast<SrtpSession*>(srtp_get_user_data(ev->session));
  if (session) {
    session->HandleEvent(ev);
  }
}

// Logs the unencrypted packet in text2pcap format. This can then be
// extracted by searching for RTP_DUMP
//   grep RTP_DUMP chrome_debug.log > in.txt
// and converted to pcap using
//   text2pcap -D -u 1000,2000 -t %H:%M:%S.%f in.txt out.pcap
// The resulting file can be replayed using the WebRTC video_replay tool and
// be inspected in Wireshark using the RTP, VP8 and H264 dissectors.
void SrtpSession::DumpPacket(const CopyOnWriteBuffer& buffer, bool outbound) {
  RTC_LOG(LS_VERBOSE) << "\n"
                      << Text2Pcap::DumpPacket(outbound, buffer,
                                               TimeUTCMillis())
                      << " # RTP_DUMP";
}

}  // namespace webrtc
