/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A TLS 1.3 test server that accepts 0-RTT early data and serves a
// canned HTTP/1.1 or HTTP/2 response. Used to exercise the Happy
// Eyeballs H1/H2 0-RTT-accepted code paths.
//
// ALPN is selected per-SNI so the H1 and H2 tests stay isolated from
// each other regardless of the client's h2 pref: the hostname
// "0rtt-accept-h1.example.com" advertises only "http/1.1" and
// "0rtt-accept-h2.example.com" advertises only "h2". To do this
// before NSS locks in the server's advertised ALPN list (which
// happens at SSL_ResetHandshake time — earlier than the SNI hook
// fires), we peek the raw ClientHello off the TCP socket with
// MSG_PEEK, parse the server_name extension, and configure the
// narrow list before the TLS layer starts. The peeked bytes stay in
// the kernel buffer and NSS re-reads them during the real handshake.
// If the peek fails (malformed ClientHello, no SNI, etc.) we fall
// back to a superset list that offers both.
//
// Accept vs reject is also per-SNI. The "0rtt-accept-*" hosts install
// the anti-replay context on each connection so NSS's server path
// decrypts and accepts incoming early-data bytes. The "0rtt-reject-*"
// hosts deliberately omit that installation — NSS then refuses the
// early data on resumption, triggers HandshakeDone with "early data
// not accepted", and drives the client's Finish0RTT(aRestart=true)
// branch. The first full handshake still succeeds and still issues a
// resumption ticket because anti-replay is only consulted when the
// server is processing incoming early-data bytes.
//
// For H1 the server reads each HTTP request in full up to the CRLF
// CRLF header terminator and replies with a canned HTTP/1.1 response.
// For H2 the server reads the connection preface and frames,
// exchanges SETTINGS, and replies to each HEADERS frame with a
// ":status 200" HEADERS frame with END_STREAM set (no body). Both
// handlers keep the TLS connection alive so post-handshake duplicate
// requests — if any were (wrongly) emitted — would be observable as
// extra handler invocations.
//
// Each request observed fires a callback to
// MOZ_ZERORTT_ACCEPT_CALLBACK_PORT at path "/callback/request" so the
// test can count exactly how many request bursts reached the HTTP
// layer.
//
// When MOZ_ZERORTT_MAX_EARLY_DATA is set, the session tickets this server
// issues allow only that many bytes of early data, so a large enough request
// exhausts the allowance before the handshake completes.

#include <stdio.h>
#include <string.h>

#include <atomic>
#include <string>

#include "ScopedNSSTypes.h"
#include "TLSServer.h"
#include "mozilla/Sprintf.h"
#include "nspr.h"
#include "ssl.h"

using namespace mozilla;
using namespace mozilla::test;

struct ZeroRttAcceptHost {
  const char* mHostName;
  const char* mCertName;
  const unsigned char* mAlpnProtos;
  size_t mAlpnProtosLen;
  // When false, we skip SSL_SetAntiReplayContext on this connection
  // so NSS refuses any incoming early-data bytes — exercising the
  // Finish0RTT(aRestart=true) path on the client.
  bool mAcceptZeroRtt;
};

const char* kHostZeroRttAcceptH1 = "0rtt-accept-h1.example.com";
const char* kHostZeroRttAcceptH2 = "0rtt-accept-h2.example.com";
const char* kHostZeroRttRejectH1 = "0rtt-reject-h1.example.com";
const char* kHostZeroRttRejectH2 = "0rtt-reject-h2.example.com";
// First connection negotiates H2 (warm-up gets a ticket); subsequent
// connections negotiate H1.  The client resumes with the H2 ticket but
// finds the server now offers only H1 → alpnChanged=1 in Finish0RTT,
// which closes the H2 session and exercises the HE winner-selection bug.
const char* kHostZeroRttAlpnSwitch = "0rtt-alpn-switch.example.com";
const char* kCertWildcard = "default-ee";

// Connection counter for kHostZeroRttAlpnSwitch: 0 means the next
// connection gets H2 (warm-up), ≥1 means it gets H1 (race).
static std::atomic<int> gAlpnSwitchCount{0};

// Wire format for SSL_SetNextProtoNego: sequence of length-prefixed
// protocol strings. Single-entry lists are immune to the NSS
// first-entry-rotation quirk.
static const unsigned char kAlpnH1Only[] = {
    0x08, 'h', 't', 't', 'p', '/', '1', '.', '1',
};
static const unsigned char kAlpnH2Only[] = {
    0x02,
    'h',
    '2',
};

MOZ_RUNINIT const ZeroRttAcceptHost sHosts[]{
    {kHostZeroRttAcceptH1, kCertWildcard, kAlpnH1Only, sizeof(kAlpnH1Only),
     true},
    {kHostZeroRttAcceptH2, kCertWildcard, kAlpnH2Only, sizeof(kAlpnH2Only),
     true},
    {kHostZeroRttRejectH1, kCertWildcard, kAlpnH1Only, sizeof(kAlpnH1Only),
     false},
    {kHostZeroRttRejectH2, kCertWildcard, kAlpnH2Only, sizeof(kAlpnH2Only),
     false},
    // ALPN-switch host: H2 on connection 0 (warm-up), H1 thereafter.
    // ALPN list is overridden in HandleHttpConnection based on
    // gAlpnSwitchCount.
    {kHostZeroRttAlpnSwitch, kCertWildcard, kAlpnH2Only, sizeof(kAlpnH2Only),
     true},
    {nullptr, nullptr, nullptr, 0, false},
};

// Callback back to the test harness. Path identifies which event is
// being reported; for this server, "/callback/request" means "we
// finished reading a full HTTP request off the wire".
int DoCallback(const char* path) {
  UniquePRFileDesc socket(PR_NewTCPSocket());
  if (!socket) {
    PrintPRError("PR_NewTCPSocket failed");
    return 1;
  }

  uint32_t port = 0;
  // Separate from MOZ_TLS_SERVER_CALLBACK_PORT, which the TLSServer
  // library consumes once at startup for its own handshake with the
  // test harness. This one is used for every subsequent per-request
  // callback we emit after TLS is up.
  const char* callbackPort = PR_GetEnv("MOZ_ZERORTT_ACCEPT_CALLBACK_PORT");
  if (callbackPort) {
    port = atoi(callbackPort);
  }
  if (!port) {
    return 0;
  }

  PRNetAddr addr;
  PR_InitializeNetAddr(PR_IpAddrLoopback, port, &addr);
  if (PR_Connect(socket.get(), &addr, PR_INTERVAL_NO_TIMEOUT) != PR_SUCCESS) {
    PrintPRError("PR_Connect failed");
    return 1;
  }

  char request[512];
  SprintfLiteral(request, "GET %s HTTP/1.0\r\n\r\n", path);
  SendAll(socket.get(), request, strlen(request));
  char buf[512];
  PR_Recv(socket.get(), buf, sizeof(buf) - 1, 0, PR_INTERVAL_NO_TIMEOUT);
  return 0;
}

// Path reported per observed request so a test can tell which code
// path delivered it:
//   /callback/request/early — server accepted 0-RTT on this
//                             connection; request bytes arrived as
//                             early data
//   /callback/request/std   — handshake finished without accepted
//                             early data; the request came post-
//                             handshake (either no 0-RTT attempted,
//                             or 0-RTT refused and the client
//                             retransmitted after Finished)
//
// NSS's SSL_GetChannelInfo only populates fields after the first
// handshake fully finishes (ss->enoughFirstHsDone). The H1 / H2
// handlers may read accepted early-data bytes before that moment.
// SSL_GetPreliminaryChannelInfo is the correct API here: the server
// sets ssl_preinfo_0rtt_cipher_suite only on 0-RTT accept.
static const char* RequestCallbackPath(PRFileDesc* aSocket) {
  SSLPreliminaryChannelInfo info;
  memset(&info, 0, sizeof(info));
  if (SSL_GetPreliminaryChannelInfo(aSocket, &info, sizeof(info)) ==
          SECSuccess &&
      (info.valuesSet & ssl_preinfo_0rtt_cipher_suite)) {
    return "/callback/request/early";
  }
  return "/callback/request/std";
}

void HandleH1Session(Connection& conn) {
  const char response[] =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 2\r\n"
      "Connection: keep-alive\r\n"
      "\r\n"
      "ok";

  std::string buffer;
  buffer.reserve(4096);
  while (true) {
    char chunk[1024];
    int32_t n =
        PR_Recv(conn.mSocket, chunk, sizeof(chunk), 0, PR_INTERVAL_NO_TIMEOUT);
    if (n <= 0) {
      return;
    }
    buffer.append(chunk, n);

    for (;;) {
      size_t end = buffer.find("\r\n\r\n");
      if (end == std::string::npos) {
        break;
      }
      buffer.erase(0, end + 4);
      DoCallback(RequestCallbackPath(conn.mSocket));
      if (NS_FAILED(SendAll(conn.mSocket, response, strlen(response)))) {
        return;
      }
    }
  }
}

// Peek the ClientHello off aSocket (MSG_PEEK — bytes stay in the
// kernel buffer for NSS to consume during the real handshake) and
// pick the ALPN list this connection should advertise. Looks up the
// SNI host_name in sHosts and returns the host's narrow list; on any
// parse / IO failure returns nullptr so the caller falls back to the
// pre-handshake superset.
//
// TLS record layout we walk:
//   record: type(1) version(2) length(2) fragment
//   handshake: type(1) length(3) body
//   ClientHello body:
//     client_version(2) random(32) session_id(1+n)
//     cipher_suites(2+n) compression_methods(1+n)
//     extensions(2+n) = [ext_type(2) ext_len(2) ext_data(ext_len)]*
//   server_name extension (type 0x0000) data:
//     list_length(2) [name_type(1) name_len(2) name(name_len)]*
static const ZeroRttAcceptHost* PeekSniAndPickHost(PRFileDesc* aSocket) {
  // 5-byte record header + up to ~4KB of ClientHello is plenty for
  // SNI. A client that can't fit SNI in 4KB is outside our test
  // matrix.
  uint8_t buf[4096];

  // A single PR_Recv with PR_MSG_PEEK only returns what is currently
  // buffered in the kernel; it doesn't wait for a specific amount.
  // When the ClientHello is relayed through a TCP proxy the bytes can
  // arrive in multiple chunks, so we may see only a prefix on the
  // first peek. Loop until we have at least the TLS record header
  // plus the declared fragment length, then the parser below can
  // inspect the full extensions block.
  size_t avail = 0;
  size_t need = 5;  // record header
  const int kMaxPeekIters = 100;
  for (int i = 0; i < kMaxPeekIters; ++i) {
    int32_t got =
        PR_Recv(aSocket, buf, sizeof(buf), PR_MSG_PEEK, PR_INTERVAL_NO_TIMEOUT);
    if (got <= 0) {
      return nullptr;
    }
    avail = static_cast<size_t>(got);
    if (avail >= 5) {
      size_t fragLen = (size_t(buf[3]) << 8) | buf[4];
      need = 5 + fragLen;
      if (need > sizeof(buf)) {
        need = sizeof(buf);
      }
    }
    if (avail >= need) {
      break;
    }
    // Small sleep so we don't busy-loop while the proxy ships the
    // rest of the fragment.
    PR_Sleep(PR_MillisecondsToInterval(5));
  }
  if (avail < 43) {
    return nullptr;
  }

  // Record header.
  if (buf[0] != 0x16) {  // handshake
    return nullptr;
  }
  size_t p = 5;
  // Handshake header.
  if (p + 4 > avail || buf[p] != 0x01) {  // ClientHello
    return nullptr;
  }
  p += 4;
  // client_version(2) + random(32).
  if (p + 34 > avail) return nullptr;
  p += 34;
  // session_id.
  if (p + 1 > avail) return nullptr;
  size_t sidLen = buf[p++];
  if (p + sidLen > avail) return nullptr;
  p += sidLen;
  // cipher_suites.
  if (p + 2 > avail) return nullptr;
  size_t csLen = (size_t(buf[p]) << 8) | buf[p + 1];
  p += 2;
  if (p + csLen > avail) return nullptr;
  p += csLen;
  // compression_methods.
  if (p + 1 > avail) return nullptr;
  size_t cmLen = buf[p++];
  if (p + cmLen > avail) return nullptr;
  p += cmLen;
  // extensions.
  if (p + 2 > avail) return nullptr;
  size_t extLen = (size_t(buf[p]) << 8) | buf[p + 1];
  p += 2;
  if (p + extLen > avail) return nullptr;
  size_t extEnd = p + extLen;

  while (p + 4 <= extEnd) {
    uint16_t extType = (uint16_t(buf[p]) << 8) | buf[p + 1];
    uint16_t extSize = (uint16_t(buf[p + 2]) << 8) | buf[p + 3];
    p += 4;
    if (p + extSize > extEnd) return nullptr;
    if (extType == 0x0000) {
      // server_name extension.
      size_t q = p;
      size_t e = p + extSize;
      if (q + 2 > e) return nullptr;
      size_t listLen = (size_t(buf[q]) << 8) | buf[q + 1];
      q += 2;
      if (q + listLen > e) return nullptr;
      size_t listEnd = q + listLen;
      while (q + 3 <= listEnd) {
        uint8_t nameType = buf[q++];
        uint16_t nameLen = (uint16_t(buf[q]) << 8) | buf[q + 1];
        q += 2;
        if (q + nameLen > listEnd) return nullptr;
        if (nameType == 0x00) {
          // host_name — match against sHosts.
          for (const ZeroRttAcceptHost* h = sHosts; h->mHostName; ++h) {
            size_t hn = strlen(h->mHostName);
            if (hn == nameLen && memcmp(h->mHostName, buf + q, hn) == 0) {
              return h;
            }
          }
          return nullptr;
        }
        q += nameLen;
      }
      return nullptr;
    }
    p += extSize;
  }
  return nullptr;
}

// Read exactly aCount bytes from aSocket into aBuf. Returns false on
// short read / error.
static bool ReadExact(PRFileDesc* aSocket, uint8_t* aBuf, size_t aCount) {
  size_t got = 0;
  while (got < aCount) {
    int32_t n =
        PR_Recv(aSocket, aBuf + got, aCount - got, 0, PR_INTERVAL_NO_TIMEOUT);
    if (n <= 0) {
      return false;
    }
    got += n;
  }
  return true;
}

// Minimal HTTP/2 responder. Reads the 24-byte connection preface,
// then loops: read 9-byte frame header, read payload (discarded
// opaquely — we don't need to HPACK-decode anything). For each
// HEADERS frame we see from the client, fire the test callback and
// write back a HEADERS frame on the same stream ID carrying a
// single HPACK-indexed :status 200 with END_HEADERS+END_STREAM.
// SETTINGS frames are responded to with our own empty SETTINGS and
// a SETTINGS ACK. Other frame types are ignored.
void HandleH2Session(Connection& conn) {
  static const uint8_t kPreface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
  constexpr size_t kPrefaceLen = sizeof(kPreface) - 1;

  uint8_t prefaceBuf[kPrefaceLen];
  if (!ReadExact(conn.mSocket, prefaceBuf, kPrefaceLen) ||
      memcmp(prefaceBuf, kPreface, kPrefaceLen) != 0) {
    return;
  }

  // Send a second session ticket so both race HCAs can each start 0-RTT
  // (SSLTokensCache::Get consumes one ticket per call).
  (void)SSL_SendSessionTicket(conn.mSocket, nullptr, 0);

  // Send our (empty) server SETTINGS frame immediately.
  static const uint8_t kServerSettings[] = {
      0x00, 0x00, 0x00,       // length = 0
      0x04,                   // type = SETTINGS
      0x00,                   // flags = 0
      0x00, 0x00, 0x00, 0x00  // stream id = 0
  };
  if (NS_FAILED(SendAll(conn.mSocket,
                        reinterpret_cast<const char*>(kServerSettings),
                        sizeof(kServerSettings)))) {
    return;
  }

  bool sentSettingsAck = false;
  while (true) {
    uint8_t header[9];
    if (!ReadExact(conn.mSocket, header, sizeof(header))) {
      return;
    }
    uint32_t length =
        (uint32_t(header[0]) << 16) | (uint32_t(header[1]) << 8) | header[2];
    uint8_t type = header[3];
    uint8_t flags = header[4];
    uint32_t streamId = ((uint32_t(header[5]) & 0x7F) << 24) |
                        (uint32_t(header[6]) << 16) |
                        (uint32_t(header[7]) << 8) | header[8];

    // Sanity cap — normal H2 frames in tests are small.
    if (length > 65536) {
      return;
    }
    std::string payload;
    payload.resize(length);
    if (length > 0 &&
        !ReadExact(conn.mSocket, reinterpret_cast<uint8_t*>(payload.data()),
                   length)) {
      return;
    }

    switch (type) {
      case 0x04: {  // SETTINGS
        if ((flags & 0x01) == 0) {
          // Client SETTINGS — reply with ACK.
          static const uint8_t kSettingsAck[] = {
              0x00, 0x00, 0x00,       // length = 0
              0x04,                   // type = SETTINGS
              0x01,                   // flags = ACK
              0x00, 0x00, 0x00, 0x00  // stream id = 0
          };
          if (NS_FAILED(SendAll(conn.mSocket,
                                reinterpret_cast<const char*>(kSettingsAck),
                                sizeof(kSettingsAck)))) {
            return;
          }
          sentSettingsAck = true;
        }
        break;
      }
      case 0x01: {  // HEADERS
        DoCallback(RequestCallbackPath(conn.mSocket));
        // HPACK: :status 200 = static table entry 8 → 0x88.
        uint8_t resp[9 + 1] = {
            0x00,
            0x00,
            0x01,  // length = 1
            0x01,  // type = HEADERS
            0x05,  // flags = END_HEADERS | END_STREAM
            uint8_t((streamId >> 24) & 0x7F),
            uint8_t((streamId >> 16) & 0xFF),
            uint8_t((streamId >> 8) & 0xFF),
            uint8_t(streamId & 0xFF),
            0x88,
        };
        if (NS_FAILED(SendAll(conn.mSocket, reinterpret_cast<const char*>(resp),
                              sizeof(resp)))) {
          return;
        }
        break;
      }
      case 0x07: {  // GOAWAY — client is shutting down.
        return;
      }
      default:
        // WINDOW_UPDATE / PING / PRIORITY / DATA / CONTINUATION / RST_STREAM:
        // ignore payload. We don't need flow-control accuracy here since
        // responses are tiny.
        break;
    }

    (void)sentSettingsAck;
  }
}

void HandleHttpConnection(PRFileDesc* aSocket,
                          const UniquePRFileDesc& aModelSocket) {
  // Pick the ALPN list per-SNI by peeking at the raw ClientHello
  // before the NSS layer is attached. Setting ALPN from the SNI
  // callback is too late — this NSS build locks in the server's
  // advertised list at SSL_ResetHandshake time, before the SNI hook
  // fires. Peeking with PR_MSG_PEEK leaves the bytes in the kernel
  // buffer so NSS still consumes them during the real handshake.
  const ZeroRttAcceptHost* sniHost = PeekSniAndPickHost(aSocket);
  fprintf(stderr, "ZeroRttAcceptServer: peek picked host=%s\n",
          sniHost ? sniHost->mHostName : "<none>");

  // Inline SetupTLS so we can inject SSL_SetNextProtoNego between
  // SSL_ImportFD and SSL_ResetHandshake. Setting ALPN after
  // SSL_ResetHandshake (what the shared SetupTLS does) appears not
  // to take effect for ALPN selection in some NSS builds.
  PRFileDesc* sslSocket = SSL_ImportFD(aModelSocket.get(), aSocket);
  if (!sslSocket) {
    PrintPRError("SSL_ImportFD failed");
    PR_Close(aSocket);
    return;
  }
  Connection conn(sslSocket);

  // NSS's SSL_SetNextProtoNego rotates the first entry to the end of
  // the list. Single-entry per-SNI lists are immune; the fallback
  // superset puts http/1.1 first so that after rotation h2 ends up
  // as the server's preferred match.
  static const unsigned char kAlpnFallback[] = {
      0x08, 'h', 't', 't', 'p', '/', '1', '.', '1', 0x02, 'h', '2',
  };
  const unsigned char* alpnProtos = kAlpnFallback;
  size_t alpnProtosLen = sizeof(kAlpnFallback);
  if (sniHost && sniHost->mAlpnProtos) {
    alpnProtos = sniHost->mAlpnProtos;
    alpnProtosLen = sniHost->mAlpnProtosLen;
  }
  // For the ALPN-switch host: connection 0 is H2 (warm-up); subsequent
  // connections use H1.  Client's H2 ticket → alpnChanged=1 → Finish0RTT
  // declares the winner before the socket closes.  Drop connection #1
  // after the handshake to leave a dead connection in mActiveConns.
  bool alpnSwitchDropAfterHandshake = false;
  if (sniHost && strcmp(sniHost->mHostName, kHostZeroRttAlpnSwitch) == 0) {
    int count = gAlpnSwitchCount.fetch_add(1);
    if (count > 0) {
      alpnProtos = kAlpnH1Only;
      alpnProtosLen = sizeof(kAlpnH1Only);
      // Drop only the first race connection (count==1). Subsequent
      // connections (retries) are served normally so the restarted
      // transaction can complete once the fix is applied.
      alpnSwitchDropAfterHandshake = (count == 1);
    }
  }
  if (SSL_SetNextProtoNego(sslSocket, alpnProtos, alpnProtosLen) !=
      SECSuccess) {
    PrintPRError("SSL_SetNextProtoNego failed on connection");
    return;
  }

  SSL_OptionSet(sslSocket, SSL_SECURITY, true);
  SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_CLIENT, false);
  SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_SERVER, true);
  SSL_OptionSet(sslSocket, SSL_ENABLE_0RTT_DATA,
                !!PR_GetEnv("MOZ_TLS_SERVER_0RTT"));
  // NSS's server-side 0-RTT path requires an anti-replay context or
  // it silently refuses every early-data byte. StartServer creates
  // one when MOZ_TLS_SERVER_0RTT is set; propagate it to this
  // connection. For "0rtt-reject-*" hosts we skip this step on
  // purpose so NSS drives the client through the 0-RTT reject path
  // (HandshakeDone reports "early data not accepted" and the txn
  // restarts after the handshake). The first full handshake still
  // issues a resumption ticket because anti-replay is only consulted
  // when the server decrypts incoming early-data bytes.
  bool acceptZeroRtt = !sniHost || sniHost->mAcceptZeroRtt;
  if (acceptZeroRtt) {
    if (SSLAntiReplayContext* antiReplay = GetAntiReplayContext()) {
      if (SSL_SetAntiReplayContext(sslSocket, antiReplay) != SECSuccess) {
        PrintPRError("SSL_SetAntiReplayContext failed");
        return;
      }
    }
  }
  SSL_ResetHandshake(sslSocket, /* asServer */ 1);

  // Drive the handshake so ALPN is actually selected before we look
  // at it.  SSL_ForceHandshake on the blocking socket returns only
  // when the full TLS exchange is done — but we ignore the return
  // value and handle the remaining handshake steps later.
  (void)SSL_ForceHandshake(sslSocket);

  // Drop after TLS completes: Finish0RTT(alpnChanged=1) has already fired on
  // the client, so the dead connection is already in mActiveConns.
  if (alpnSwitchDropAfterHandshake) {
    return;
  }

  SSLNextProtoState state = SSL_NEXT_PROTO_NO_SUPPORT;
  uint8_t protoBuf[32] = {0};
  unsigned int protoLen = 0;
  if (SSL_GetNextProto(conn.mSocket, &state, protoBuf, &protoLen,
                       sizeof(protoBuf)) != SECSuccess) {
    protoLen = 0;
  }

  if (protoLen == 2 && memcmp(protoBuf, "h2", 2) == 0) {
    HandleH2Session(conn);
  } else {
    HandleH1Session(conn);
  }
}

int32_t DoSNISocketConfig(PRFileDesc* aFd, const SECItem* aSrvNameArr,
                          uint32_t aSrvNameArrSize, void* /*aArg*/) {
  const ZeroRttAcceptHost* host =
      GetHostForSNI(aSrvNameArr, aSrvNameArrSize, sHosts);
  if (!host) {
    return SSL_SNI_SEND_ALERT;
  }

  UniqueCERTCertificate cert;
  SSLKEAType certKEA;
  if (SECSuccess != ConfigSecureServerWithNamedCert(aFd, host->mCertName, &cert,
                                                    &certKEA, nullptr)) {
    return SSL_SNI_SEND_ALERT;
  }
  // ALPN is set per-SNI before SSL_ResetHandshake in
  // HandleHttpConnection by peeking the raw ClientHello; we don't
  // update it here because this NSS build won't honor a late
  // SSL_SetNextProtoNego from the SNI hook.
  return 0;
}

SECStatus ConfigureServer(PRFileDesc* aFd) {
  // TLSServer's default SetupTLS doesn't enable session tickets. In
  // TLS 1.3 they're the only mechanism for session resumption / PSK,
  // and without them the server can't offer the client a chance to do
  // 0-RTT on the next connection. Enabled here so our test can
  // exercise an actual 0-RTT-accepted handshake.
  if (SSL_OptionSet(aFd, SSL_ENABLE_SESSION_TICKETS, true) != SECSuccess) {
    PrintPRError("SSL_OptionSet SSL_ENABLE_SESSION_TICKETS failed");
    return SECFailure;
  }

  // Advertise ALPN: prefer h2, fall back to http/1.1. NSS's server
  // picks the first advertised protocol that the client also offers.
  // Format: sequence of length-prefixed strings.
  // NSS's SSL_SetNextProtoNego rotates the first entry to the end of
  // the list (legacy NPN fallback semantics). Put http/1.1 first so
  // that after rotation h2 ends up as the server's preferred match.
  static const unsigned char kAlpnProtos[] = {
      0x08, 'h', 't', 't', 'p', '/', '1', '.', '1', 0x02, 'h', '2',
  };
  if (SSL_SetNextProtoNego(aFd, kAlpnProtos, sizeof(kAlpnProtos)) !=
      SECSuccess) {
    PrintPRError("SSL_SetNextProtoNego failed");
    return SECFailure;
  }

  if (const char* maxEarlyData = PR_GetEnv("MOZ_ZERORTT_MAX_EARLY_DATA")) {
    if (SSL_SetMaxEarlyDataSize(aFd, atoi(maxEarlyData)) != SECSuccess) {
      PrintPRError("SSL_SetMaxEarlyDataSize failed");
      return SECFailure;
    }
  }
  return SECSuccess;
}

int main(int argc, char* argv[]) {
  int rv = StartServer(argc, argv, DoSNISocketConfig, nullptr, ConfigureServer,
                       HandleHttpConnection);
  if (rv < 0) {
    return rv;
  }
}
