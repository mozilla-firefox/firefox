/*
 *  Copyright 2014 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/ssl_adapter.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "rtc_base/gunit.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/message_digest.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/stream.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/gmock.h"

using ::testing::_;
using ::testing::Return;

static const int kTimeout = 5000;

static rtc::Socket* CreateSocket(const rtc::SSLMode& ssl_mode) {
  rtc::SocketAddress address(rtc::IPAddress(INADDR_ANY), 0);

  rtc::Socket* socket = rtc::Thread::Current()->socketserver()->CreateSocket(
      address.family(),
      (ssl_mode == rtc::SSL_MODE_DTLS) ? SOCK_DGRAM : SOCK_STREAM);
  socket->Bind(address);

  return socket;
}

static std::string GetSSLProtocolName(const rtc::SSLMode& ssl_mode) {
  return (ssl_mode == rtc::SSL_MODE_DTLS) ? "DTLS" : "TLS";
}

// Simple mock for the certificate verifier.
class MockCertVerifier : public rtc::SSLCertificateVerifier {
 public:
  virtual ~MockCertVerifier() = default;
  MOCK_METHOD(bool, Verify, (const rtc::SSLCertificate&), (override));
};

// TODO(benwright) - Move to using INSTANTIATE_TEST_SUITE_P instead of using
// duplicate test cases for simple parameter changes.
class SSLAdapterTestDummyClient : public sigslot::has_slots<> {
 public:
  explicit SSLAdapterTestDummyClient(const rtc::SSLMode& ssl_mode)
      : ssl_mode_(ssl_mode) {
    rtc::Socket* socket = CreateSocket(ssl_mode_);

    ssl_adapter_.reset(rtc::SSLAdapter::Create(socket));

    ssl_adapter_->SetMode(ssl_mode_);

    // Ignore any certificate errors for the purpose of testing.
    // Note: We do this only because we don't have a real certificate.
    // NEVER USE THIS IN PRODUCTION CODE!
    ssl_adapter_->SetIgnoreBadCert(true);

    ssl_adapter_->SignalReadEvent.connect(
        this, &SSLAdapterTestDummyClient::OnSSLAdapterReadEvent);
    ssl_adapter_->SignalCloseEvent.connect(
        this, &SSLAdapterTestDummyClient::OnSSLAdapterCloseEvent);
  }

  void SetIgnoreBadCert(bool ignore_bad_cert) {
    ssl_adapter_->SetIgnoreBadCert(ignore_bad_cert);
  }

  void SetCertVerifier(rtc::SSLCertificateVerifier* ssl_cert_verifier) {
    ssl_adapter_->SetCertVerifier(ssl_cert_verifier);
  }

  void SetAlpnProtocols(const std::vector<std::string>& protos) {
    ssl_adapter_->SetAlpnProtocols(protos);
  }

  void SetEllipticCurves(const std::vector<std::string>& curves) {
    ssl_adapter_->SetEllipticCurves(curves);
  }

  rtc::SocketAddress GetAddress() const {
    return ssl_adapter_->GetLocalAddress();
  }

  rtc::Socket::ConnState GetState() const { return ssl_adapter_->GetState(); }

  const std::string& GetReceivedData() const { return data_; }

  int Connect(absl::string_view hostname, const rtc::SocketAddress& address) {
    RTC_LOG(LS_INFO) << "Initiating connection with " << address.ToString();

    int rv = ssl_adapter_->Connect(address);

    if (rv == 0) {
      RTC_LOG(LS_INFO) << "Starting " << GetSSLProtocolName(ssl_mode_)
                       << " handshake with " << hostname;

      if (ssl_adapter_->StartSSL(hostname) != 0) {
        return -1;
      }
    }

    return rv;
  }

  int Close() { return ssl_adapter_->Close(); }

  int Send(absl::string_view message) {
    RTC_LOG(LS_INFO) << "Client sending '" << message << "'";

    return ssl_adapter_->Send(message.data(), message.length());
  }

  void OnSSLAdapterReadEvent(rtc::Socket* socket) {
    char buffer[4096] = "";

    // Read data received from the server and store it in our internal buffer.
    int read = socket->Recv(buffer, sizeof(buffer) - 1, nullptr);
    if (read != -1) {
      buffer[read] = '\0';

      RTC_LOG(LS_INFO) << "Client received '" << buffer << "'";

      data_ += buffer;
    }
  }

  void OnSSLAdapterCloseEvent(rtc::Socket* socket, int error) {
    // OpenSSLAdapter signals handshake failure with a close event, but without
    // closing the socket! Let's close the socket here. This way GetState() can
    // return CS_CLOSED after failure.
    if (socket->GetState() != rtc::Socket::CS_CLOSED) {
      socket->Close();
    }
  }

 private:
  const rtc::SSLMode ssl_mode_;

  std::unique_ptr<rtc::SSLAdapter> ssl_adapter_;

  std::string data_;
};

namespace {

class SocketStream : public rtc::StreamInterface, public sigslot::has_slots<> {
 public:
  explicit SocketStream(rtc::Socket* socket) : socket_(socket) {
    socket_->SignalConnectEvent.connect(this, &SocketStream::OnConnectEvent);
    socket_->SignalReadEvent.connect(this, &SocketStream::OnReadEvent);
    socket_->SignalWriteEvent.connect(this, &SocketStream::OnWriteEvent);
    socket_->SignalCloseEvent.connect(this, &SocketStream::OnCloseEvent);
  }

  ~SocketStream() override = default;

  rtc::StreamState GetState() const override {
    switch (socket_->GetState()) {
      case rtc::Socket::CS_CONNECTED:
        return rtc::SS_OPEN;
      case rtc::Socket::CS_CONNECTING:
        return rtc::SS_OPENING;
      case rtc::Socket::CS_CLOSED:
      default:
        return rtc::SS_CLOSED;
    }
  }

  rtc::StreamResult Read(rtc::ArrayView<uint8_t> buffer,
                         size_t& read,
                         int& error) override {
    int result = socket_->Recv(buffer.data(), buffer.size(), nullptr);
    if (result < 0) {
      if (socket_->IsBlocking())
        return rtc::SR_BLOCK;
      error = socket_->GetError();
      return rtc::SR_ERROR;
    }
    if ((result > 0) || (buffer.size() == 0)) {
      read = result;
      return rtc::SR_SUCCESS;
    }
    return rtc::SR_EOS;
  }

  rtc::StreamResult Write(rtc::ArrayView<const uint8_t> data,
                          size_t& written,
                          int& error) override {
    int result = socket_->Send(data.data(), data.size());
    if (result < 0) {
      if (socket_->IsBlocking())
        return rtc::SR_BLOCK;
      error = socket_->GetError();
      return rtc::SR_ERROR;
    }
    written = result;
    return rtc::SR_SUCCESS;
  }

  void Close() override { socket_->Close(); }

 private:
  void OnConnectEvent(rtc::Socket* socket) {
    RTC_DCHECK_EQ(socket, socket_.get());
    SignalEvent(this, rtc::SE_OPEN | rtc::SE_READ | rtc::SE_WRITE, 0);
  }

  void OnReadEvent(rtc::Socket* socket) {
    RTC_DCHECK_EQ(socket, socket_.get());
    SignalEvent(this, rtc::SE_READ, 0);
  }
  void OnWriteEvent(rtc::Socket* socket) {
    RTC_DCHECK_EQ(socket, socket_.get());
    SignalEvent(this, rtc::SE_WRITE, 0);
  }
  void OnCloseEvent(rtc::Socket* socket, int err) {
    RTC_DCHECK_EQ(socket, socket_.get());
    SignalEvent(this, rtc::SE_CLOSE, err);
  }

  std::unique_ptr<rtc::Socket> socket_;
};

}  // namespace

class SSLAdapterTestDummyServer : public sigslot::has_slots<> {
 public:
  explicit SSLAdapterTestDummyServer(const rtc::SSLMode& ssl_mode,
                                     const rtc::KeyParams& key_params)
      : ssl_mode_(ssl_mode) {
    // Generate a key pair and a certificate for this host.
    ssl_identity_ = rtc::SSLIdentity::Create(GetHostname(), key_params);

    server_socket_.reset(CreateSocket(ssl_mode_));

    if (ssl_mode_ == rtc::SSL_MODE_TLS) {
      server_socket_->SignalReadEvent.connect(
          this, &SSLAdapterTestDummyServer::OnServerSocketReadEvent);

      server_socket_->Listen(1);
    }

    RTC_LOG(LS_INFO) << ((ssl_mode_ == rtc::SSL_MODE_DTLS) ? "UDP" : "TCP")
                     << " server listening on "
                     << server_socket_->GetLocalAddress().ToString();
  }

  rtc::SocketAddress GetAddress() const {
    return server_socket_->GetLocalAddress();
  }

  std::string GetHostname() const {
    // Since we don't have a real certificate anyway, the value here doesn't
    // really matter.
    return "example.com";
  }

  const std::string& GetReceivedData() const { return data_; }

  int Send(absl::string_view message) {
    if (ssl_stream_adapter_ == nullptr ||
        ssl_stream_adapter_->GetState() != rtc::SS_OPEN) {
      // No connection yet.
      return -1;
    }

    RTC_LOG(LS_INFO) << "Server sending '" << message << "'";

    size_t written;
    int error;

    rtc::StreamResult r = ssl_stream_adapter_->Write(
        rtc::MakeArrayView(reinterpret_cast<const uint8_t*>(message.data()),
                           message.size()),
        written, error);
    if (r == rtc::SR_SUCCESS) {
      return written;
    } else {
      return -1;
    }
  }

  void AcceptConnection(const rtc::SocketAddress& address) {
    // Only a single connection is supported.
    ASSERT_TRUE(ssl_stream_adapter_ == nullptr);

    // This is only for DTLS.
    ASSERT_EQ(rtc::SSL_MODE_DTLS, ssl_mode_);

    // Transfer ownership of the socket to the SSLStreamAdapter object.
    rtc::Socket* socket = server_socket_.release();

    socket->Connect(address);

    DoHandshake(socket);
  }

  void OnServerSocketReadEvent(rtc::Socket* socket) {
    // Only a single connection is supported.
    ASSERT_TRUE(ssl_stream_adapter_ == nullptr);

    DoHandshake(server_socket_->Accept(nullptr));
  }

  void OnSSLStreamAdapterEvent(rtc::StreamInterface* stream, int sig, int err) {
    if (sig & rtc::SE_READ) {
      uint8_t buffer[4096] = "";
      size_t read;
      int error;

      // Read data received from the client and store it in our internal
      // buffer.
      rtc::StreamResult r = stream->Read(buffer, read, error);
      if (r == rtc::SR_SUCCESS) {
        buffer[read] = '\0';
        // Here we assume that the buffer is interpretable as string.
        char* buffer_as_char = reinterpret_cast<char*>(buffer);
        RTC_LOG(LS_INFO) << "Server received '" << buffer_as_char << "'";
        data_ += buffer_as_char;
      }
    }
  }

 private:
  void DoHandshake(rtc::Socket* socket) {
    ssl_stream_adapter_ =
        rtc::SSLStreamAdapter::Create(std::make_unique<SocketStream>(socket));

    ssl_stream_adapter_->SetMode(ssl_mode_);
    ssl_stream_adapter_->SetServerRole();

    // SSLStreamAdapter is normally used for peer-to-peer communication, but
    // here we're testing communication between a client and a server
    // (e.g. a WebRTC-based application and an RFC 5766 TURN server), where
    // clients are not required to provide a certificate during handshake.
    // Accordingly, we must disable client authentication here.
    ssl_stream_adapter_->SetClientAuthEnabledForTesting(false);

    ssl_stream_adapter_->SetIdentity(ssl_identity_->Clone());

    // Set a bogus peer certificate digest.
    unsigned char digest[20];
    size_t digest_len = sizeof(digest);
    ssl_stream_adapter_->SetPeerCertificateDigest(rtc::DIGEST_SHA_1, digest,
                                                  digest_len);

    ssl_stream_adapter_->StartSSL();

    ssl_stream_adapter_->SignalEvent.connect(
        this, &SSLAdapterTestDummyServer::OnSSLStreamAdapterEvent);
  }

  const rtc::SSLMode ssl_mode_;

  std::unique_ptr<rtc::Socket> server_socket_;
  std::unique_ptr<rtc::SSLStreamAdapter> ssl_stream_adapter_;

  std::unique_ptr<rtc::SSLIdentity> ssl_identity_;

  std::string data_;
};

class SSLAdapterTestBase : public ::testing::Test, public sigslot::has_slots<> {
 public:
  explicit SSLAdapterTestBase(const rtc::SSLMode& ssl_mode,
                              const rtc::KeyParams& key_params)
      : ssl_mode_(ssl_mode),
        vss_(new rtc::VirtualSocketServer()),
        thread_(vss_.get()),
        server_(new SSLAdapterTestDummyServer(ssl_mode_, key_params)),
        client_(new SSLAdapterTestDummyClient(ssl_mode_)),
        handshake_wait_(kTimeout) {}

  void SetHandshakeWait(int wait) { handshake_wait_ = wait; }

  void SetIgnoreBadCert(bool ignore_bad_cert) {
    client_->SetIgnoreBadCert(ignore_bad_cert);
  }

  void SetCertVerifier(rtc::SSLCertificateVerifier* ssl_cert_verifier) {
    client_->SetCertVerifier(ssl_cert_verifier);
  }

  void SetAlpnProtocols(const std::vector<std::string>& protos) {
    client_->SetAlpnProtocols(protos);
  }

  void SetEllipticCurves(const std::vector<std::string>& curves) {
    client_->SetEllipticCurves(curves);
  }

  void SetMockCertVerifier(bool return_value) {
    auto mock_verifier = std::make_unique<MockCertVerifier>();
    EXPECT_CALL(*mock_verifier, Verify(_)).WillRepeatedly(Return(return_value));
    cert_verifier_ =
        std::unique_ptr<rtc::SSLCertificateVerifier>(std::move(mock_verifier));

    SetIgnoreBadCert(false);
    SetCertVerifier(cert_verifier_.get());
  }

  void TestHandshake(bool expect_success) {
    int rv;

    // The initial state is CS_CLOSED
    ASSERT_EQ(rtc::Socket::CS_CLOSED, client_->GetState());

    rv = client_->Connect(server_->GetHostname(), server_->GetAddress());
    ASSERT_EQ(0, rv);

    // Now the state should be CS_CONNECTING
    ASSERT_EQ(rtc::Socket::CS_CONNECTING, client_->GetState());

    if (ssl_mode_ == rtc::SSL_MODE_DTLS) {
      // For DTLS, call AcceptConnection() with the client's address.
      server_->AcceptConnection(client_->GetAddress());
    }

    if (expect_success) {
      // If expecting success, the client should end up in the CS_CONNECTED
      // state after handshake.
      EXPECT_EQ_WAIT(rtc::Socket::CS_CONNECTED, client_->GetState(),
                     handshake_wait_);

      RTC_LOG(LS_INFO) << GetSSLProtocolName(ssl_mode_)
                       << " handshake complete.";

    } else {
      // On handshake failure the client should end up in the CS_CLOSED state.
      EXPECT_EQ_WAIT(rtc::Socket::CS_CLOSED, client_->GetState(),
                     handshake_wait_);

      RTC_LOG(LS_INFO) << GetSSLProtocolName(ssl_mode_) << " handshake failed.";
    }
  }

  void TestTransfer(absl::string_view message) {
    int rv;

    rv = client_->Send(message);
    ASSERT_EQ(static_cast<int>(message.length()), rv);

    // The server should have received the client's message.
    EXPECT_EQ_WAIT(message, server_->GetReceivedData(), kTimeout);

    rv = server_->Send(message);
    ASSERT_EQ(static_cast<int>(message.length()), rv);

    // The client should have received the server's message.
    EXPECT_EQ_WAIT(message, client_->GetReceivedData(), kTimeout);

    RTC_LOG(LS_INFO) << "Transfer complete.";
  }

 protected:
  const rtc::SSLMode ssl_mode_;

  std::unique_ptr<rtc::VirtualSocketServer> vss_;
  rtc::AutoSocketServerThread thread_;
  std::unique_ptr<SSLAdapterTestDummyServer> server_;
  std::unique_ptr<SSLAdapterTestDummyClient> client_;
  std::unique_ptr<rtc::SSLCertificateVerifier> cert_verifier_;

  int handshake_wait_;
};

class SSLAdapterTestTLS_RSA : public SSLAdapterTestBase {
 public:
  SSLAdapterTestTLS_RSA()
      : SSLAdapterTestBase(rtc::SSL_MODE_TLS, rtc::KeyParams::RSA()) {}
};

class SSLAdapterTestTLS_ECDSA : public SSLAdapterTestBase {
 public:
  SSLAdapterTestTLS_ECDSA()
      : SSLAdapterTestBase(rtc::SSL_MODE_TLS, rtc::KeyParams::ECDSA()) {}
};

class SSLAdapterTestDTLS_RSA : public SSLAdapterTestBase {
 public:
  SSLAdapterTestDTLS_RSA()
      : SSLAdapterTestBase(rtc::SSL_MODE_DTLS, rtc::KeyParams::RSA()) {}
};

class SSLAdapterTestDTLS_ECDSA : public SSLAdapterTestBase {
 public:
  SSLAdapterTestDTLS_ECDSA()
      : SSLAdapterTestBase(rtc::SSL_MODE_DTLS, rtc::KeyParams::ECDSA()) {}
};

// Basic tests: TLS

// Test that handshake works, using RSA
TEST_F(SSLAdapterTestTLS_RSA, TestTLSConnect) {
  TestHandshake(true);
}

// Test that handshake works with a custom verifier that returns true. RSA.
TEST_F(SSLAdapterTestTLS_RSA, TestTLSConnectCustomCertVerifierSucceeds) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
}

// Test that handshake fails with a custom verifier that returns false. RSA.
TEST_F(SSLAdapterTestTLS_RSA, TestTLSConnectCustomCertVerifierFails) {
  SetMockCertVerifier(/*return_value=*/false);
  TestHandshake(/*expect_success=*/false);
}

// Test that handshake works, using ECDSA
TEST_F(SSLAdapterTestTLS_ECDSA, TestTLSConnect) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
}

// Test that handshake works with a custom verifier that returns true. ECDSA.
TEST_F(SSLAdapterTestTLS_ECDSA, TestTLSConnectCustomCertVerifierSucceeds) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
}

// Test that handshake fails with a custom verifier that returns false. ECDSA.
TEST_F(SSLAdapterTestTLS_ECDSA, TestTLSConnectCustomCertVerifierFails) {
  SetMockCertVerifier(/*return_value=*/false);
  TestHandshake(/*expect_success=*/false);
}

// Test transfer between client and server, using RSA
TEST_F(SSLAdapterTestTLS_RSA, TestTLSTransfer) {
  TestHandshake(true);
  TestTransfer("Hello, world!");
}

// Test transfer between client and server, using RSA with custom cert verifier.
TEST_F(SSLAdapterTestTLS_RSA, TestTLSTransferCustomCertVerifier) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
  TestTransfer("Hello, world!");
}

TEST_F(SSLAdapterTestTLS_RSA, TestTLSTransferWithBlockedSocket) {
  TestHandshake(true);

  // Tell the underlying socket to simulate being blocked.
  vss_->SetSendingBlocked(true);

  std::string expected;
  int rv;
  // Send messages until the SSL socket adapter starts applying backpressure.
  // Note that this may not occur immediately since there may be some amount of
  // intermediate buffering (either in our code or in BoringSSL).
  for (int i = 0; i < 1024; ++i) {
    std::string message = "Hello, world: " + rtc::ToString(i);
    rv = client_->Send(message);
    if (rv != static_cast<int>(message.size())) {
      // This test assumes either the whole message or none of it is sent.
      ASSERT_EQ(-1, rv);
      break;
    }
    expected += message;
  }
  // Assert that the loop above exited due to Send returning -1.
  ASSERT_EQ(-1, rv);

  // Try sending another message while blocked. -1 should be returned again and
  // it shouldn't end up received by the server later.
  EXPECT_EQ(-1, client_->Send("Never sent"));

  // Unblock the underlying socket. All of the buffered messages should be sent
  // without any further action.
  vss_->SetSendingBlocked(false);
  EXPECT_EQ_WAIT(expected, server_->GetReceivedData(), kTimeout);

  // Send another message. This previously wasn't working
  std::string final_message = "Fin.";
  expected += final_message;
  EXPECT_EQ(static_cast<int>(final_message.size()),
            client_->Send(final_message));
  EXPECT_EQ_WAIT(expected, server_->GetReceivedData(), kTimeout);
}

// Test transfer between client and server, using ECDSA
TEST_F(SSLAdapterTestTLS_ECDSA, TestTLSTransfer) {
  TestHandshake(true);
  TestTransfer("Hello, world!");
}

// Test transfer between client and server, using ECDSA with custom cert
// verifier.
TEST_F(SSLAdapterTestTLS_ECDSA, TestTLSTransferCustomCertVerifier) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
  TestTransfer("Hello, world!");
}

// Test transfer using ALPN with protos as h2 and http/1.1
TEST_F(SSLAdapterTestTLS_ECDSA, TestTLSALPN) {
  std::vector<std::string> alpn_protos{"h2", "http/1.1"};
  SetAlpnProtocols(alpn_protos);
  TestHandshake(true);
  TestTransfer("Hello, world!");
}

// Test transfer with TLS Elliptic curves set to "X25519:P-256:P-384:P-521"
TEST_F(SSLAdapterTestTLS_ECDSA, TestTLSEllipticCurves) {
  std::vector<std::string> elliptic_curves{"X25519", "P-256", "P-384", "P-521"};
  SetEllipticCurves(elliptic_curves);
  TestHandshake(true);
  TestTransfer("Hello, world!");
}

// Basic tests: DTLS

// Test that handshake works, using RSA
TEST_F(SSLAdapterTestDTLS_RSA, TestDTLSConnect) {
  TestHandshake(true);
}

// Test that handshake works with a custom verifier that returns true. DTLS_RSA.
TEST_F(SSLAdapterTestDTLS_RSA, TestDTLSConnectCustomCertVerifierSucceeds) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
}

// Test that handshake fails with a custom verifier that returns false.
// DTLS_RSA.
TEST_F(SSLAdapterTestDTLS_RSA, TestTLSConnectCustomCertVerifierFails) {
  SetMockCertVerifier(/*return_value=*/false);
  TestHandshake(/*expect_success=*/false);
}

// Test that handshake works, using ECDSA
TEST_F(SSLAdapterTestDTLS_ECDSA, TestDTLSConnect) {
  TestHandshake(true);
}

// Test that handshake works with a custom verifier that returns true.
// DTLS_ECDSA.
TEST_F(SSLAdapterTestDTLS_ECDSA, TestDTLSConnectCustomCertVerifierSucceeds) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
}

// Test that handshake fails with a custom verifier that returns false.
// DTLS_ECDSA.
TEST_F(SSLAdapterTestDTLS_ECDSA, TestTLSConnectCustomCertVerifierFails) {
  SetMockCertVerifier(/*return_value=*/false);
  TestHandshake(/*expect_success=*/false);
}

// Test transfer between client and server, using RSA
TEST_F(SSLAdapterTestDTLS_RSA, TestDTLSTransfer) {
  TestHandshake(true);
  TestTransfer("Hello, world!");
}

// Test transfer between client and server, using RSA with custom cert verifier.
TEST_F(SSLAdapterTestDTLS_RSA, TestDTLSTransferCustomCertVerifier) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
  TestTransfer("Hello, world!");
}

// Test transfer between client and server, using ECDSA
TEST_F(SSLAdapterTestDTLS_ECDSA, TestDTLSTransfer) {
  TestHandshake(true);
  TestTransfer("Hello, world!");
}

// Test transfer between client and server, using ECDSA with custom cert
// verifier.
TEST_F(SSLAdapterTestDTLS_ECDSA, TestDTLSTransferCustomCertVerifier) {
  SetMockCertVerifier(/*return_value=*/true);
  TestHandshake(/*expect_success=*/true);
  TestTransfer("Hello, world!");
}
