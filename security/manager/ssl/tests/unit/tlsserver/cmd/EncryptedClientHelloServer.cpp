/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a standalone server that offers TLS 1.3 Encrypted
// Client Hello support. When MOZ_TLS_ECH_PROXY_PORT is set, it acts as a
// reverse proxy instead: each connection is decrypted and relayed to a
// plaintext server on that local port.

#include <pk11pub.h>
#include <stdio.h>

#include <memory>
#include <thread>
#include <vector>

#include "ScopedNSSTypes.h"
#include "TLSServer.h"
#include "nspr.h"
#include "ssl.h"
#include "sslexp.h"

using namespace mozilla;
using namespace mozilla::test;

struct EchHost {
  const char* mHostName;
  const char* mCertName;
};

MOZ_RUNINIT const std::vector<uint32_t> kSuiteChaCha = {
    (static_cast<uint32_t>(HpkeKdfHkdfSha256) << 16) |
    HpkeAeadChaCha20Poly1305};

// Hostname, cert nickname pairs.
const EchHost sEchHosts[] = {{"ech-public.example.com", "default-ee"},
                             {"ech-private.example.com", "private-ee"},
                             {"selfsigned.example.com", "selfsigned"},
                             {nullptr, nullptr}};

int32_t DoSNISocketConfigBySubjectCN(PRFileDesc* aFd,
                                     const SECItem* aSrvNameArr,
                                     uint32_t aSrvNameArrSize) {
  for (uint32_t i = 0; i < aSrvNameArrSize; i++) {
    UniquePORTString name(
        static_cast<char*>(PORT_ZAlloc(aSrvNameArr[i].len + 1)));
    if (name) {
      PORT_Memcpy(name.get(), aSrvNameArr[i].data, aSrvNameArr[i].len);
      if (ConfigSecureServerWithNamedCert(aFd, name.get(), nullptr, nullptr,
                                          nullptr) == SECSuccess) {
        return 0;
      }
    }
  }

  return SSL_SNI_SEND_ALERT;
}

int32_t DoSNISocketConfig(PRFileDesc* aFd, const SECItem* aSrvNameArr,
                          uint32_t aSrvNameArrSize, void* /*aArg*/) {
  const EchHost* host = GetHostForSNI(aSrvNameArr, aSrvNameArrSize, sEchHosts);
  if (!host) {
    PrintPRError("No cert found for hostname");
    return SSL_SNI_SEND_ALERT;
  }

  if (gDebugLevel >= DEBUG_VERBOSE) {
    fprintf(stderr, "found pre-defined host '%s'\n", host->mHostName);
  }

  UniqueCERTCertificate cert;
  SSLKEAType certKEA;
  if (SECSuccess != ConfigSecureServerWithNamedCert(aFd, host->mCertName, &cert,
                                                    &certKEA, nullptr)) {
    return SSL_SNI_SEND_ALERT;
  }

  return 0;
}

int32_t SetAlpnOptions(PRFileDesc* aFd, uint8_t flags) {
  const std::vector<uint8_t> http1 = {0x08, 0x68, 0x74, 0x74, 0x70,
                                      0x2f, 0x31, 0x2e, 0x31};
  const std::vector<uint8_t> http2 = {0x02, 0x68, 0x32};
  const std::vector<uint8_t> http3 = {0x02, 0x68, 0x33};
  std::vector<uint8_t> alpnVec = {};
  if (flags & 0b001) {
    alpnVec.insert(alpnVec.end(), http1.begin(), http1.end());
  }
  if (flags & 0b010) {
    alpnVec.insert(alpnVec.end(), http2.begin(), http2.end());
  }
  if (flags & 0b100) {
    alpnVec.insert(alpnVec.end(), http3.begin(), http3.end());
  }
  fprintf(stderr, "ALPN Flags: %u\n", flags);
  fprintf(stderr, "ALPN length: %zu\n", alpnVec.size());
  if (SSL_SetNextProtoNego(aFd, alpnVec.data(), alpnVec.size()) != SECSuccess) {
    fprintf(stderr, "Setting ALPN failed!\n");
    return 1;
  }

  return 0;
}

SECStatus ConfigureServer(PRFileDesc* aFd) {
  const char* alpnFlag = PR_GetEnv("MOZ_TLS_ECH_ALPN_FLAG");
  if (alpnFlag) {
    uint8_t flag = atoi(alpnFlag);
    SetAlpnOptions(aFd, flag);
  }

  UniquePK11SlotInfo slot(PK11_GetInternalKeySlot());
  if (!slot) {
    PrintPRError("PK11_GetInternalKeySlot failed");
    return SECFailure;
  }

  UniqueSECKEYPublicKey pubKey;
  UniqueSECKEYPrivateKey privKey;
  SECKEYPublicKey* tmpPubKey = nullptr;
  SECKEYPrivateKey* tmpPrivKey = nullptr;

  static const std::vector<uint8_t> pkcs8{
      0x30, 0x67, 0x02, 0x01, 0x00, 0x30, 0x14, 0x06, 0x07, 0x2a, 0x86, 0x48,
      0xce, 0x3d, 0x02, 0x01, 0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xda,
      0x47, 0x0f, 0x01, 0x04, 0x4c, 0x30, 0x4a, 0x02, 0x01, 0x01, 0x04, 0x20,
      0x8c, 0x49, 0x0e, 0x5b, 0x0c, 0x7d, 0xbe, 0x0c, 0x6d, 0x21, 0x92, 0x48,
      0x4d, 0x2b, 0x7a, 0x04, 0x23, 0xb3, 0xb4, 0x54, 0x4f, 0x24, 0x81, 0x09,
      0x5a, 0x99, 0xdb, 0xf2, 0x38, 0xfb, 0x35, 0x0f, 0xa1, 0x23, 0x03, 0x21,
      0x00, 0x8a, 0x07, 0x56, 0x39, 0x49, 0xfa, 0xc6, 0x23, 0x29, 0x36, 0xed,
      0x6f, 0x36, 0xc4, 0xfa, 0x73, 0x59, 0x30, 0xec, 0xde, 0xae, 0xf6, 0x73,
      0x4e, 0x31, 0x4a, 0xea, 0xc3, 0x5a, 0x56, 0xfd, 0x0a};

  SECItem pkcs8Item = {siBuffer, const_cast<uint8_t*>(pkcs8.data()),
                       static_cast<unsigned int>(pkcs8.size())};
  SECStatus rv = PK11_ImportDERPrivateKeyInfoAndReturnKey(
      slot.get(), &pkcs8Item, nullptr, nullptr, false, false, KU_ALL,
      &tmpPrivKey, nullptr);

  if (rv != SECSuccess) {
    PrintPRError("PK11_ImportDERPrivateKeyInfoAndReturnKey failed");
    return SECFailure;
  }
  privKey.reset(tmpPrivKey);
  tmpPubKey = SECKEY_ConvertToPublicKey(privKey.get());
  pubKey.reset(tmpPubKey);

  if (!privKey || !pubKey) {
    PrintPRError("ECH/HPKE Public or Private key is null!");
    return SECFailure;
  }

  std::vector<uint8_t> echConfig(1000, 0);
  unsigned int len = 0;
  const PRUint8 configId = 77;
  const HpkeSymmetricSuite echCipherSuite = {HpkeKdfHkdfSha256,
                                             HpkeAeadChaCha20Poly1305};
  rv = SSL_EncodeEchConfigId(configId, "ech-public.example.com", 100,
                             HpkeDhKemX25519Sha256, pubKey.get(),
                             &echCipherSuite, 1, echConfig.data(), &len,
                             echConfig.size());
  if (rv != SECSuccess) {
    PrintPRError("SSL_EncodeEchConfig failed");
    return rv;
  }

  rv = SSL_SetServerEchConfigs(aFd, pubKey.get(), privKey.get(),
                               echConfig.data(), len);
  if (rv != SECSuccess) {
    PrintPRError("SSL_SetServerEchConfigs failed");
    return rv;
  }

  return SECSuccess;
}

static uint16_t sProxyPort = 0;

static bool SendRaw(PRFileDesc* aSocket, const char* aData, int32_t aLen) {
  while (aLen > 0) {
    int32_t sent = PR_Send(aSocket, aData, aLen, 0, PR_INTERVAL_NO_TIMEOUT);
    if (sent <= 0) {
      return false;
    }
    aData += sent;
    aLen -= sent;
  }
  return true;
}

// Copies data in both directions until either side closes.
static void Relay(PRFileDesc* aClient, PRFileDesc* aBackend) {
  PRPollDesc pds[2] = {{aClient, PR_POLL_READ, 0}, {aBackend, PR_POLL_READ, 0}};
  char buf[16384];
  while (PR_Poll(pds, 2, PR_INTERVAL_NO_TIMEOUT) > 0) {
    for (int i = 0; i < 2; i++) {
      if (!pds[i].out_flags) {
        continue;
      }
      int32_t n =
          PR_Recv(pds[i].fd, buf, sizeof(buf), 0, PR_INTERVAL_NO_TIMEOUT);
      if (n <= 0 || !SendRaw(pds[1 - i].fd, buf, n)) {
        return;
      }
    }
  }
}

static void ProxyConnection(PRFileDesc* aSocket,
                            const UniquePRFileDesc& aModelSocket) {
  auto conn = std::make_unique<Connection>(aSocket);
  if (NS_FAILED(SetupTLS(conn.get(), aModelSocket.get()))) {
    return;
  }

  std::thread([conn = std::move(conn)] {
    if (SSL_ForceHandshake(conn->mSocket) != SECSuccess) {
      PrintPRError("SSL_ForceHandshake failed");
      return;
    }

    UniquePRFileDesc backend(PR_NewTCPSocket());
    PRNetAddr addr;
    PR_InitializeNetAddr(PR_IpAddrLoopback, sProxyPort, &addr);
    if (!backend || PR_Connect(backend.get(), &addr, PR_INTERVAL_NO_TIMEOUT) !=
                        PR_SUCCESS) {
      PrintPRError("PR_Connect to the proxy backend failed");
      return;
    }

    Relay(conn->mSocket, backend.get());
  }).detach();
}

int main(int argc, char* argv[]) {
  const char* proxyPort = PR_GetEnv("MOZ_TLS_ECH_PROXY_PORT");
  if (proxyPort) {
    sProxyPort = atoi(proxyPort);
  }

  int rv = StartServer(argc, argv, DoSNISocketConfig, nullptr, ConfigureServer,
                       sProxyPort ? ProxyConnection : nullptr);
  if (rv < 0) {
    return rv;
  }
}
