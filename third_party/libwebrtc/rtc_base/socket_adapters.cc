/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#if defined(_MSC_VER) && _MSC_VER < 1300
#pragma warning(disable : 4786)
#endif

#include "rtc_base/socket_adapters.h"

#include <algorithm>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/zero_memory.h"

namespace rtc {

BufferedReadAdapter::BufferedReadAdapter(Socket* socket, size_t size)
    : AsyncSocketAdapter(socket),
      buffer_size_(size),
      data_len_(0),
      buffering_(false) {
  buffer_ = new char[buffer_size_];
}

BufferedReadAdapter::~BufferedReadAdapter() {
  delete[] buffer_;
}

int BufferedReadAdapter::Send(const void* pv, size_t cb) {
  if (buffering_) {
    // TODO: Spoof error better; Signal Writeable
    SetError(EWOULDBLOCK);
    return -1;
  }
  return AsyncSocketAdapter::Send(pv, cb);
}

int BufferedReadAdapter::Recv(void* pv, size_t cb, int64_t* timestamp) {
  if (buffering_) {
    SetError(EWOULDBLOCK);
    return -1;
  }

  size_t read = 0;

  if (data_len_) {
    read = std::min(cb, data_len_);
    memcpy(pv, buffer_, read);
    data_len_ -= read;
    if (data_len_ > 0) {
      memmove(buffer_, buffer_ + read, data_len_);
    }
    pv = static_cast<char*>(pv) + read;
    cb -= read;
  }

  // FIX: If cb == 0, we won't generate another read event

  int res = AsyncSocketAdapter::Recv(pv, cb, timestamp);
  if (res >= 0) {
    // Read from socket and possibly buffer; return combined length
    return res + static_cast<int>(read);
  }

  if (read > 0) {
    // Failed to read from socket, but still read something from buffer
    return static_cast<int>(read);
  }

  // Didn't read anything; return error from socket
  return res;
}

void BufferedReadAdapter::BufferInput(bool on) {
  buffering_ = on;
}

void BufferedReadAdapter::OnReadEvent(Socket* socket) {
  RTC_DCHECK(socket == GetSocket());

  if (!buffering_) {
    AsyncSocketAdapter::OnReadEvent(socket);
    return;
  }

  if (data_len_ >= buffer_size_) {
    RTC_LOG(LS_ERROR) << "Input buffer overflow";
    RTC_DCHECK_NOTREACHED();
    data_len_ = 0;
  }

  int len = AsyncSocketAdapter::Recv(buffer_ + data_len_,
                                     buffer_size_ - data_len_, nullptr);
  if (len < 0) {
    // TODO: Do something better like forwarding the error to the user.
    RTC_LOG_ERR(LS_INFO) << "Recv";
    return;
  }

  data_len_ += len;

  ProcessInput(buffer_, &data_len_);
}

///////////////////////////////////////////////////////////////////////////////

// This is a SSL v2 CLIENT_HELLO message.
// TODO: Should this have a session id? The response doesn't have a
// certificate, so the hello should have a session id.
static const uint8_t kSslClientHello[] = {
    0x80, 0x46,                                            // msg len
    0x01,                                                  // CLIENT_HELLO
    0x03, 0x01,                                            // SSL 3.1
    0x00, 0x2d,                                            // ciphersuite len
    0x00, 0x00,                                            // session id len
    0x00, 0x10,                                            // challenge len
    0x01, 0x00, 0x80, 0x03, 0x00, 0x80, 0x07, 0x00, 0xc0,  // ciphersuites
    0x06, 0x00, 0x40, 0x02, 0x00, 0x80, 0x04, 0x00, 0x80,  //
    0x00, 0x00, 0x04, 0x00, 0xfe, 0xff, 0x00, 0x00, 0x0a,  //
    0x00, 0xfe, 0xfe, 0x00, 0x00, 0x09, 0x00, 0x00, 0x64,  //
    0x00, 0x00, 0x62, 0x00, 0x00, 0x03, 0x00, 0x00, 0x06,  //
    0x1f, 0x17, 0x0c, 0xa6, 0x2f, 0x00, 0x78, 0xfc,        // challenge
    0x46, 0x55, 0x2e, 0xb1, 0x83, 0x39, 0xf1, 0xea         //
};

// static
ArrayView<const uint8_t> AsyncSSLSocket::SslClientHello() {
  // Implicit conversion directly from kSslClientHello to ArrayView fails when
  // built with gcc.
  return {kSslClientHello, sizeof(kSslClientHello)};
}

// This is a TLSv1 SERVER_HELLO message.
static const uint8_t kSslServerHello[] = {
    0x16,                                            // handshake message
    0x03, 0x01,                                      // SSL 3.1
    0x00, 0x4a,                                      // message len
    0x02,                                            // SERVER_HELLO
    0x00, 0x00, 0x46,                                // handshake len
    0x03, 0x01,                                      // SSL 3.1
    0x42, 0x85, 0x45, 0xa7, 0x27, 0xa9, 0x5d, 0xa0,  // server random
    0xb3, 0xc5, 0xe7, 0x53, 0xda, 0x48, 0x2b, 0x3f,  //
    0xc6, 0x5a, 0xca, 0x89, 0xc1, 0x58, 0x52, 0xa1,  //
    0x78, 0x3c, 0x5b, 0x17, 0x46, 0x00, 0x85, 0x3f,  //
    0x20,                                            // session id len
    0x0e, 0xd3, 0x06, 0x72, 0x5b, 0x5b, 0x1b, 0x5f,  // session id
    0x15, 0xac, 0x13, 0xf9, 0x88, 0x53, 0x9d, 0x9b,  //
    0xe8, 0x3d, 0x7b, 0x0c, 0x30, 0x32, 0x6e, 0x38,  //
    0x4d, 0xa2, 0x75, 0x57, 0x41, 0x6c, 0x34, 0x5c,  //
    0x00, 0x04,                                      // RSA/RC4-128/MD5
    0x00                                             // null compression
};

// static
ArrayView<const uint8_t> AsyncSSLSocket::SslServerHello() {
  return {kSslServerHello, sizeof(kSslServerHello)};
}

AsyncSSLSocket::AsyncSSLSocket(Socket* socket)
    : BufferedReadAdapter(socket, 1024) {}

int AsyncSSLSocket::Connect(const SocketAddress& addr) {
  // Begin buffering before we connect, so that there isn't a race condition
  // between potential senders and receiving the OnConnectEvent signal
  BufferInput(true);
  return BufferedReadAdapter::Connect(addr);
}

void AsyncSSLSocket::OnConnectEvent(Socket* socket) {
  RTC_DCHECK(socket == GetSocket());
  // TODO: we could buffer output too...
  const int res = DirectSend(kSslClientHello, sizeof(kSslClientHello));
  if (res != sizeof(kSslClientHello)) {
    RTC_LOG(LS_ERROR) << "Sending fake SSL ClientHello message failed.";
    Close();
    SignalCloseEvent(this, 0);
  }
}

void AsyncSSLSocket::ProcessInput(char* data, size_t* len) {
  if (*len < sizeof(kSslServerHello))
    return;

  if (memcmp(kSslServerHello, data, sizeof(kSslServerHello)) != 0) {
    RTC_LOG(LS_ERROR) << "Received non-matching fake SSL ServerHello message.";
    Close();
    SignalCloseEvent(this, 0);  // TODO: error code?
    return;
  }

  *len -= sizeof(kSslServerHello);
  if (*len > 0) {
    memmove(data, data + sizeof(kSslServerHello), *len);
  }

  bool remainder = (*len > 0);
  BufferInput(false);
  SignalConnectEvent(this);

  // FIX: if SignalConnect causes the socket to be destroyed, we are in trouble
  if (remainder)
    SignalReadEvent(this);
}

}  // namespace rtc
