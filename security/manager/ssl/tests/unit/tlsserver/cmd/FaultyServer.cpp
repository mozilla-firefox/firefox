/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>

#include "ScopedNSSTypes.h"
#include "TLSServer.h"
#include "mozilla/Sprintf.h"
#include "nspr.h"
#include "ssl.h"
#include "ssl3prot.h"
#include "sslexp.h"
#include "sslimpl.h"

using namespace mozilla;
using namespace mozilla::test;

enum FaultType {
  None = 0,
  ZeroRtt,
  UnknownSNI,
  Mlkem768x25519,
  Mlkem1024,
};

struct FaultyServerHost {
  const char* mHostName;
  const char* mCertName;
  FaultType mFaultType;
};

const char* kHostOk = "ok.example.com";
const char* kHostUnknown = "unknown.example.com";
const char* kHostZeroRttAlertBadMac = "0rtt-alert-bad-mac.example.com";
const char* kHostZeroRttAlertVersion =
    "0rtt-alert-protocol-version.example.com";
const char* kHostZeroRttAlertUnexpected = "0rtt-alert-unexpected.example.com";
const char* kHostZeroRttAlertDowngrade = "0rtt-alert-downgrade.example.com";
const char* kHostDecryptErrorOnResume = "decrypt-error-on-resume.example.com";
const char* kHostIllegalParameterOnResume =
    "illegal-parameter-on-resume.example.com";
const char* kHostPSKDecryptErrorNoEarlyData =
    "psk-no-early-data-on-resume.example.com";

const char* kHostMlkem768x25519NetInterrupt =
    "mlkem768x25519-net-interrupt.example.com";
const char* kHostMlkem768x25519AlertAfterServerHello =
    "mlkem768x25519-alert-after-server-hello.example.com";

const char* kHostMlkem1024 = "mlkem1024.example.com";

const char* kCertWildcard = "default-ee";

/* Each type of failure gets a different SNI.
 * the "default-ee" cert has a SAN for *.example.com
 * the "no-san-ee" cert is signed by the test-ca, but it doesn't have any SANs.
 */
MOZ_RUNINIT const FaultyServerHost sFaultyServerHosts[]{
    {kHostOk, kCertWildcard, None},
    {kHostUnknown, kCertWildcard, UnknownSNI},
    {kHostZeroRttAlertBadMac, kCertWildcard, ZeroRtt},
    {kHostZeroRttAlertVersion, kCertWildcard, ZeroRtt},
    {kHostZeroRttAlertUnexpected, kCertWildcard, ZeroRtt},
    {kHostZeroRttAlertDowngrade, kCertWildcard, ZeroRtt},
    {kHostDecryptErrorOnResume, kCertWildcard, ZeroRtt},
    {kHostIllegalParameterOnResume, kCertWildcard, ZeroRtt},
    {kHostPSKDecryptErrorNoEarlyData, kCertWildcard, ZeroRtt},
    {kHostMlkem768x25519NetInterrupt, kCertWildcard, Mlkem768x25519},
    {kHostMlkem768x25519AlertAfterServerHello, kCertWildcard, Mlkem768x25519},
    {kHostMlkem1024, kCertWildcard, Mlkem1024},
    {nullptr, nullptr},
};

// returns 0 on success, non-zero on error
int DoCallback(const char* path) {
  UniquePRFileDesc socket(PR_NewTCPSocket());
  if (!socket) {
    PrintPRError("PR_NewTCPSocket failed");
    return 1;
  }

  uint32_t port = 0;
  const char* callbackPort = PR_GetEnv("FAULTY_SERVER_CALLBACK_PORT");
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
  char buf[4096];
  memset(buf, 0, sizeof(buf));
  int32_t bytesRead =
      PR_Recv(socket.get(), buf, sizeof(buf) - 1, 0, PR_INTERVAL_NO_TIMEOUT);
  if (bytesRead < 0) {
    PrintPRError("PR_Recv failed 1");
    return 1;
  }
  if (bytesRead == 0) {
    fprintf(stderr, "PR_Recv eof 1\n");
    return 1;
  }
  // fprintf(stderr, "%s\n", buf);
  return 0;
}

/* These are very rough examples. In practice the `arg` parameter to a callback
 * might need to be an object that holds some state, like the various traffic
 * secrets. */

/* An SSLSecretCallback is called after every key derivation step in the TLS
 * 1.3 key schedule.
 *
 * Epoch 1 is for the early traffic secret.
 * Epoch 2 is for the handshake traffic secrets.
 * Epoch 3 is for the application traffic secrets.
 */
void SecretCallbackFailZeroRtt(PRFileDesc* fd, PRUint16 epoch,
                               SSLSecretDirection dir, PK11SymKey* /*secret*/,
                               void* arg) {
  fprintf(stderr, "0RTT handler epoch=%d dir=%d\n", epoch, (uint32_t)dir);
  FaultyServerHost* host = static_cast<FaultyServerHost*>(arg);

  if (epoch == 1 && dir == ssl_secret_read) {
    sslSocket* ss = ssl_FindSocket(fd);
    if (!ss) {
      fprintf(stderr, "0RTT handler, no ss!\n");
      return;
    }

    char path[256];
    SprintfLiteral(path, "/callback/%d", epoch);
    DoCallback(path);

    fprintf(stderr, "0RTT handler, configuring alert\n");
    if (!strcmp(host->mHostName, kHostZeroRttAlertBadMac)) {
      SSL3_SendAlert(ss, alert_fatal, bad_record_mac);
    } else if (!strcmp(host->mHostName, kHostZeroRttAlertVersion)) {
      SSL3_SendAlert(ss, alert_fatal, protocol_version);
    } else if (!strcmp(host->mHostName, kHostZeroRttAlertUnexpected)) {
      SSL3_SendAlert(ss, alert_fatal, unexpected_message);
    } else if (!strcmp(host->mHostName, kHostDecryptErrorOnResume)) {
      SSL3_SendAlert(ss, alert_fatal, decrypt_error);
    } else if (!strcmp(host->mHostName, kHostIllegalParameterOnResume)) {
      SSL3_SendAlert(ss, alert_fatal, illegal_parameter);
    }
  } else if (epoch == 2 && dir == ssl_secret_read &&
             !strcmp(host->mHostName, kHostPSKDecryptErrorNoEarlyData)) {
    sslSocket* ss = ssl_FindSocket(fd);
    if (!ss) {
      fprintf(stderr, "PSK-no-earlydata handler, no ss!\n");
      return;
    }
    if (!ss->statelessResume) {
      return;
    }
    fprintf(stderr, "PSK-no-earlydata handler, sending alert\n");
    char path[256];
    SprintfLiteral(path, "/callback/%d", epoch);
    DoCallback(path);
    SSL3_SendAlert(ss, alert_fatal, decrypt_error);
  }
}

SECStatus FailingWriteCallback(PRFileDesc* /*fd*/, PRUint16 /*epoch*/,
                               SSLContentType /*contentType*/,
                               const PRUint8* /*data*/, unsigned int /*len*/,
                               void* /*arg*/) {
  return SECFailure;
}

void SecretCallbackFailMlkem768x25519(PRFileDesc* fd, PRUint16 epoch,
                                      SSLSecretDirection dir,
                                      PK11SymKey* /*secret*/, void* arg) {
  fprintf(stderr, "Mlkem768x25519 handler epoch=%d dir=%d\n", epoch,
          (uint32_t)dir);
  FaultyServerHost* host = static_cast<FaultyServerHost*>(arg);

  if (epoch == 2 && dir == ssl_secret_write) {
    sslSocket* ss = ssl_FindSocket(fd);
    if (!ss) {
      fprintf(stderr, "Mlkem768x25519 handler, no ss!\n");
      return;
    }

    if (!ss->sec.keaGroup) {
      fprintf(stderr, "Mlkem768x25519 handler, no ss->sec.keaGroup!\n");
      return;
    }

    char path[256];
    SprintfLiteral(path, "/callback/%u", ss->sec.keaGroup->name);
    DoCallback(path);

    if (ss->sec.keaGroup->name != ssl_grp_kem_mlkem768x25519) {
      return;
    }

    fprintf(stderr, "Mlkem768x25519 handler, configuring alert\n");
    if (strcmp(host->mHostName, kHostMlkem768x25519NetInterrupt) == 0) {
      // Install a record write callback that causes the next write to fail.
      // The client will see this as a PR_END_OF_FILE / NS_ERROR_NET_INTERRUPT
      // error.
      ss->recordWriteCallback = FailingWriteCallback;
    } else if (!strcmp(host->mHostName,
                       kHostMlkem768x25519AlertAfterServerHello)) {
      SSL3_SendAlert(ss, alert_fatal, close_notify);
    }
  }
}

int32_t DoSNISocketConfig(PRFileDesc* aFd, const SECItem* aSrvNameArr,
                          uint32_t aSrvNameArrSize, void* /*aArg*/) {
  const FaultyServerHost* host =
      GetHostForSNI(aSrvNameArr, aSrvNameArrSize, sFaultyServerHosts);
  if (!host || host->mFaultType == UnknownSNI) {
    PrintPRError("No cert found for hostname");
    return SSL_SNI_SEND_ALERT;
  }

  if (gDebugLevel >= DEBUG_VERBOSE) {
    fprintf(stderr, "found pre-defined host '%s'\n", host->mHostName);
  }

  const SSLNamedGroup hybridTestNamedGroups[] = {ssl_grp_kem_mlkem768x25519,
                                                 ssl_grp_ec_curve25519};

  const SSLNamedGroup mlkem1024TestNamedGroups[] = {ssl_grp_kem_mlkem1024};

  switch (host->mFaultType) {
    case ZeroRtt:
      SSL_SecretCallback(aFd, &SecretCallbackFailZeroRtt, (void*)host);
      break;
    case Mlkem768x25519:
      SSL_SecretCallback(aFd, &SecretCallbackFailMlkem768x25519, (void*)host);
      SSL_NamedGroupConfig(aFd, hybridTestNamedGroups,
                           std::size(hybridTestNamedGroups));
      break;
    case Mlkem1024:
      SSL_NamedGroupConfig(aFd, mlkem1024TestNamedGroups,
                           std::size(mlkem1024TestNamedGroups));
      break;
    case None:
      break;
    default:
      break;
  }

  UniqueCERTCertificate cert;
  SSLKEAType certKEA;
  if (SECSuccess != ConfigSecureServerWithNamedCert(aFd, host->mCertName, &cert,
                                                    &certKEA, nullptr)) {
    return SSL_SNI_SEND_ALERT;
  }

  return 0;
}

SECStatus ConfigureServer(PRFileDesc* /*aFd*/) { return SECSuccess; }

int main(int argc, char* argv[]) {
  int rv = StartServer(argc, argv, DoSNISocketConfig, nullptr, ConfigureServer);
  if (rv < 0) {
    return rv;
  }
}
