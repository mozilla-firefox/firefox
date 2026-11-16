/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TLSServer.h"

#include <stdio.h>

#include <fstream>
#include <string>
#include <thread>
#include <vector>
#ifdef XP_WIN
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include <utility>

#include "base64.h"
#include "certdb.h"
#include "mozilla/Sprintf.h"
#include "nspr.h"
#include "nss.h"
#include "plarenas.h"
#include "prenv.h"
#include "prerror.h"
#include "prnetdb.h"
#include "prtime.h"
#include "ssl.h"
#include "sslexp.h"
#include "sslproto.h"

namespace mozilla {
namespace test {

static const uint16_t LISTEN_PORT = 8443;

SSLAntiReplayContext* antiReplay = nullptr;

SSLAntiReplayContext* GetAntiReplayContext() { return antiReplay; }

DebugLevel gDebugLevel = DEBUG_ERRORS;
uint16_t gCallbackPort = 0;

static const char kPEMBegin[] = "-----BEGIN ";
static const char kPEMEnd[] = "-----END ";
const char DEFAULT_CERT_NICKNAME[] = "default-ee";

Connection::Connection(PRFileDesc* aSocket) : mSocket(aSocket), mByte(0) {}

Connection::~Connection() {
  if (mSocket) {
    PR_Close(mSocket);
  }
}

void PrintPRError(const char* aPrefix) {
  const char* err = PR_ErrorToName(PR_GetError());
  if (err) {
    if (gDebugLevel >= DEBUG_ERRORS) {
      fprintf(stderr, "%s: %s\n", aPrefix, err);
    }
  } else {
    if (gDebugLevel >= DEBUG_ERRORS) {
      fprintf(stderr, "%s\n", aPrefix);
    }
  }
}

// This decodes a PEM file into `item`. The line endings need to be
// UNIX-style, or there will be cross-platform issues.
static bool DecodePEMFile(const std::string& filename, SECItem* item) {
  std::ifstream in(filename);
  if (in.bad()) {
    return false;
  }

  char buf[1024];
  in.getline(buf, sizeof(buf));
  if (in.bad()) {
    return false;
  }

  if (strncmp(buf, kPEMBegin, std::string::traits_type::length(kPEMBegin)) !=
      0) {
    return false;
  }

  std::string value;
  for (;;) {
    in.getline(buf, sizeof(buf));
    if (in.bad()) {
      return false;
    }

    if (strncmp(buf, kPEMEnd, std::string::traits_type::length(kPEMEnd)) == 0) {
      break;
    }

    value += buf;
  }

  unsigned int binLength;
  UniquePORTString bin(BitwiseCast<char*, unsigned char*>(
      ATOB_AsciiToData(value.c_str(), &binLength)));
  if (!bin || binLength == 0) {
    PrintPRError("ATOB_AsciiToData failed");
    return false;
  }

  if (SECITEM_AllocItem(nullptr, item, binLength) == nullptr) {
    return false;
  }

  PORT_Memcpy(item->data, bin.get(), binLength);
  return true;
}

static SECStatus AddKeyFromFile(const std::string& path,
                                const std::string& filename) {
  ScopedAutoSECItem item;

  std::string file = path + "/" + filename;
  if (!DecodePEMFile(file, &item)) {
    return SECFailure;
  }

  UniquePK11SlotInfo slot(PK11_GetInternalKeySlot());
  if (!slot) {
    PrintPRError("PK11_GetInternalKeySlot failed");
    return SECFailure;
  }

  if (PK11_NeedUserInit(slot.get())) {
    if (PK11_InitPin(slot.get(), nullptr, nullptr) != SECSuccess) {
      PrintPRError("PK11_InitPin failed");
      return SECFailure;
    }
  }

  SECKEYPrivateKey* privateKey = nullptr;
  SECItem nick = {siBuffer,
                  BitwiseCast<unsigned char*, const char*>(filename.data()),
                  static_cast<unsigned int>(filename.size())};
  if (PK11_ImportDERPrivateKeyInfoAndReturnKey(
          slot.get(), &item, &nick, nullptr, true, false, KU_ALL, &privateKey,
          nullptr) != SECSuccess) {
    PrintPRError("PK11_ImportDERPrivateKeyInfoAndReturnKey failed");
    return SECFailure;
  }

  SECKEY_DestroyPrivateKey(privateKey);
  return SECSuccess;
}

static SECStatus AddCertificateFromFile(const std::string& path,
                                        const std::string& filename) {
  ScopedAutoSECItem item;

  std::string file = path + "/" + filename;
  if (!DecodePEMFile(file, &item)) {
    return SECFailure;
  }

  UniqueCERTCertificate cert(CERT_NewTempCertificate(
      CERT_GetDefaultCertDB(), &item, nullptr, false, true));
  if (!cert) {
    PrintPRError("CERT_NewTempCertificate failed");
    return SECFailure;
  }

  UniquePK11SlotInfo slot(PK11_GetInternalKeySlot());
  if (!slot) {
    PrintPRError("PK11_GetInternalKeySlot failed");
    return SECFailure;
  }
  // The nickname is the filename without '.pem'.
  std::string nickname = filename.substr(0, filename.length() - 4);
  SECStatus rv = PK11_ImportCert(slot.get(), cert.get(), CK_INVALID_HANDLE,
                                 nickname.c_str(), false);
  if (rv != SECSuccess) {
    PrintPRError("PK11_ImportCert failed");
    return rv;
  }

  // By convention, the file `test-ca.pem` is a trust anchor.
  if (!filename.compare("test-ca.pem")) {
    if (PK11_NeedUserInit(slot.get())) {
      rv = PK11_InitPin(slot.get(), nullptr, nullptr);
      if (rv != SECSuccess) {
        PrintPRError("PK11_InitPin failed");
        return rv;
      }
    }
    rv = PK11_CheckUserPassword(slot.get(), "");
    if (rv != SECSuccess) {
      PrintPRError("PK11_CheckUserPassword failed");
      return rv;
    }
    CERTCertTrust trust{
        CERTDB_TERMINAL_RECORD | CERTDB_TRUSTED_CA | CERTDB_TRUSTED_CLIENT_CA,
        0, 0};
    rv = CERT_ChangeCertTrust(nullptr, cert.get(), &trust);
    if (rv != SECSuccess) {
      PrintPRError("CERT_ChangeCertTrust failed");
      return rv;
    }
  }

  return SECSuccess;
}

SECStatus LoadCertificatesAndKeys(const char* basePath) {
  // The NSS cert DB path could have been specified as "sql:path". Trim off
  // the leading "sql:" if so.
  if (strncmp(basePath, "sql:", 4) == 0) {
    basePath = basePath + 4;
  }

  UniquePRDir fdDir(PR_OpenDir(basePath));
  if (!fdDir) {
    PrintPRError("PR_OpenDir failed");
    return SECFailure;
  }
  // On the B2G ICS emulator, operations taken in AddCertificateFromFile
  // appear to interact poorly with readdir (more specifically, something is
  // causing readdir to never return null - it indefinitely loops through every
  // file in the directory, which causes timeouts). Rather than waste more time
  // chasing this down, loading certificates and keys happens in two phases:
  // filename collection and then loading. (This is probably a good
  // idea anyway because readdir isn't reentrant. Something could change later
  // such that it gets called as a result of calling AddCertificateFromFile or
  // AddKeyFromFile.)
  std::vector<std::string> certificates;
  std::vector<std::string> keys;
  for (PRDirEntry* dirEntry = PR_ReadDir(fdDir.get(), PR_SKIP_BOTH); dirEntry;
       dirEntry = PR_ReadDir(fdDir.get(), PR_SKIP_BOTH)) {
    size_t nameLength = strlen(dirEntry->name);
    if (nameLength > 4) {
      if (strncmp(dirEntry->name + nameLength - 4, ".pem", 4) == 0) {
        certificates.push_back(dirEntry->name);
      } else if (strncmp(dirEntry->name + nameLength - 4, ".key", 4) == 0) {
        keys.push_back(dirEntry->name);
      }
    }
  }
  SECStatus rv;
  for (std::string& certificate : certificates) {
    rv = AddCertificateFromFile(basePath, certificate.c_str());
    if (rv != SECSuccess) {
      return rv;
    }
  }
  for (std::string& key : keys) {
    rv = AddKeyFromFile(basePath, key.c_str());
    if (rv != SECSuccess) {
      return rv;
    }
  }
  return SECSuccess;
}

SECStatus InitializeNSS(const char* nssCertDBDir) {
  // Try initializing an existing DB.
  if (NSS_Init(nssCertDBDir) == SECSuccess) {
    return SECSuccess;
  }

  // Create a new DB if there is none...
  SECStatus rv = NSS_Initialize(nssCertDBDir, nullptr, nullptr, nullptr, 0);
  if (rv != SECSuccess) {
    return rv;
  }

  // ...and load all certificates into it.
  return LoadCertificatesAndKeys(nssCertDBDir);
}

nsresult SendAll(PRFileDesc* aSocket, const char* aData, size_t aDataLen) {
  if (gDebugLevel >= DEBUG_VERBOSE) {
    fprintf(stderr, "sending '%s'\n", aData);
  }

  while (aDataLen > 0) {
    int32_t bytesSent =
        PR_Send(aSocket, aData, aDataLen, 0, PR_INTERVAL_NO_TIMEOUT);
    if (bytesSent == -1) {
      PrintPRError("PR_Send failed");
      return NS_ERROR_FAILURE;
    }

    aDataLen -= bytesSent;
    aData += bytesSent;
  }

  return NS_OK;
}

nsresult ReplyToRequest(Connection* aConn) {
  // For debugging purposes, SendAll can print out what it's sending.
  // So, any strings we give to it to send need to be null-terminated.
  char buf[2] = {aConn->mByte, 0};
  return SendAll(aConn->mSocket, buf, 1);
}

nsresult SetupTLS(Connection* aConn, PRFileDesc* aModelSocket) {
  PRFileDesc* sslSocket = SSL_ImportFD(aModelSocket, aConn->mSocket);
  if (!sslSocket) {
    PrintPRError("SSL_ImportFD failed");
    return NS_ERROR_FAILURE;
  }
  aConn->mSocket = sslSocket;

  /* anti-replay must be configured to accept 0RTT */
  if (antiReplay) {
    SECStatus rv = SSL_SetAntiReplayContext(sslSocket, antiReplay);
    if (rv != SECSuccess) {
      PrintPRError("error configuring anti-replay ");
      return NS_ERROR_FAILURE;
    }
  }

  SSL_OptionSet(sslSocket, SSL_SECURITY, true);
  SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_CLIENT, false);
  SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_SERVER, true);
  // Unconditionally enabling 0RTT makes test_session_resumption.js fail
  SSL_OptionSet(sslSocket, SSL_ENABLE_0RTT_DATA,
                !!PR_GetEnv("MOZ_TLS_SERVER_0RTT"));

  SSL_ResetHandshake(sslSocket, /* asServer */ 1);

  return NS_OK;
}

nsresult ReadRequest(Connection* aConn) {
  int32_t bytesRead =
      PR_Recv(aConn->mSocket, &aConn->mByte, 1, 0, PR_INTERVAL_NO_TIMEOUT);
  if (bytesRead < 0) {
    PrintPRError("PR_Recv failed");
    return NS_ERROR_FAILURE;
  } else if (bytesRead == 0) {
    PR_SetError(PR_IO_ERROR, 0);
    PrintPRError("PR_Recv EOF in ReadRequest");
    return NS_ERROR_FAILURE;
  } else {
    if (gDebugLevel >= DEBUG_VERBOSE) {
      fprintf(stderr, "read '0x%hhx'\n", aConn->mByte);
    }
  }
  return NS_OK;
}

void HandleConnection(PRFileDesc* aSocket,
                      const UniquePRFileDesc& aModelSocket) {
  Connection conn(aSocket);
  nsresult rv = SetupTLS(&conn, aModelSocket.get());
  if (NS_FAILED(rv)) {
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    PrintPRError("PR_Recv failed");
    exit(1);
  }

  // TODO: On tests that are expected to fail (e.g. due to a revoked
  // certificate), the client will close the connection wtihout sending us the
  // request byte. In those cases, we should keep going. But, in the cases
  // where the connection is supposed to suceed, we should verify that we
  // successfully receive the request and send the response.
  rv = ReadRequest(&conn);
  if (NS_SUCCEEDED(rv)) {
    rv = ReplyToRequest(&conn);
  }
}

// returns 0 on success, non-zero on error
int DoCallback() {
  UniquePRFileDesc socket(PR_NewTCPSocket());
  if (!socket) {
    PrintPRError("PR_NewTCPSocket failed");
    return 1;
  }

  PRNetAddr addr;
  PR_InitializeNetAddr(PR_IpAddrLoopback, gCallbackPort, &addr);
  if (PR_Connect(socket.get(), &addr, PR_INTERVAL_NO_TIMEOUT) != PR_SUCCESS) {
    PrintPRError("PR_Connect failed");
    return 1;
  }

  const char* request = "GET / HTTP/1.0\r\n\r\n";
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
  fprintf(stderr, "%s\n", buf);
  return 0;
}

SECStatus ConfigSecureServerWithNamedCert(
    PRFileDesc* fd, const char* certName,
    /*optional*/ UniqueCERTCertificate* certOut,
    /*optional*/ SSLKEAType* keaOut,
    /*optional*/ SSLExtraServerCertData* extraData) {
  UniqueCERTCertificate cert(PK11_FindCertFromNickname(certName, nullptr));
  if (!cert) {
    PrintPRError("PK11_FindCertFromNickname failed");
    return SECFailure;
  }
  // If an intermediate certificate issued the server certificate (rather than
  // directly by a trust anchor), we want to send it along in the handshake so
  // we don't encounter unknown issuer errors when that's not what we're
  // testing.
  UniqueCERTCertificateList certList;
  UniqueCERTCertificate issuerCert(
      CERT_FindCertByName(CERT_GetDefaultCertDB(), &cert->derIssuer));
  // If we can't find the issuer cert, continue without it.
  if (issuerCert) {
    // Sadly, CERTCertificateList does not have a CERT_NewCertificateList
    // utility function, so we must create it ourselves. This consists
    // of creating an arena, allocating space for the CERTCertificateList,
    // and then transferring ownership of the arena to that list.
    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      PrintPRError("PORT_NewArena failed");
      return SECFailure;
    }
    certList.reset(static_cast<CERTCertificateList*>(
        PORT_ArenaAlloc(arena.get(), sizeof(CERTCertificateList))));
    if (!certList) {
      PrintPRError("PORT_ArenaAlloc failed");
      return SECFailure;
    }
    certList->arena = arena.release();
    // We also have to manually copy the certificates we care about to the
    // list, because there aren't any utility functions for that either.
    certList->certs = static_cast<SECItem*>(
        PORT_ArenaAlloc(certList->arena, 2 * sizeof(SECItem)));
    if (SECITEM_CopyItem(certList->arena, certList->certs, &cert->derCert) !=
        SECSuccess) {
      PrintPRError("SECITEM_CopyItem failed");
      return SECFailure;
    }
    if (SECITEM_CopyItem(certList->arena, certList->certs + 1,
                         &issuerCert->derCert) != SECSuccess) {
      PrintPRError("SECITEM_CopyItem failed");
      return SECFailure;
    }
    certList->len = 2;
  }

  UniquePK11SlotInfo slot(PK11_GetInternalKeySlot());
  if (!slot) {
    PrintPRError("PK11_GetInternalKeySlot failed");
    return SECFailure;
  }
  UniqueSECKEYPrivateKey key(
      PK11_FindKeyByDERCert(slot.get(), cert.get(), nullptr));
  if (!key) {
    PrintPRError("PK11_FindKeyByDERCert failed");
    return SECFailure;
  }

  // SSL_ConfigServerCert is the preferred way to configure the server
  // certificate, but it is too strict for the inadequate key usage tests.
  SSLKEAType certKEA = NSS_FindCertKEAType(cert.get());
  if (cert->keyUsage & KU_DIGITAL_SIGNATURE) {
    SSLExtraServerCertData dataCopy = {ssl_auth_null, nullptr, nullptr,
                                       nullptr,       nullptr, nullptr};
    if (extraData) {
      memcpy(&dataCopy, extraData, sizeof(dataCopy));
    }
    dataCopy.certChain = certList.get();

    if (SSL_ConfigServerCert(fd, cert.get(), key.get(), &dataCopy,
                             sizeof(dataCopy)) != SECSuccess) {
      PrintPRError("SSL_ConfigServerCert failed");
      return SECFailure;
    }
  } else {
    if (SSL_ConfigSecureServerWithCertChain(fd, cert.get(), certList.get(),
                                            key.get(), certKEA) != SECSuccess) {
      PrintPRError("SSL_ConfigSecureServerWithCertChain failed");
      return SECFailure;
    }
  }

  if (keaOut) {
    *keaOut = certKEA;
  }

  if (certOut) {
    *certOut = std::move(cert);
  }

  SSL_OptionSet(fd, SSL_NO_CACHE, false);
  // NSS can't protect session tickets with an ML-DSA server key, and fails the
  // connection if it tries (bug 2065004).
  SSL_OptionSet(fd, SSL_ENABLE_SESSION_TICKETS, key->keyType != mldsaKey);
  // Unconditionally enabling 0RTT makes test_session_resumption.js fail
  SSL_OptionSet(fd, SSL_ENABLE_0RTT_DATA, !!PR_GetEnv("MOZ_TLS_SERVER_0RTT"));

  return SECSuccess;
}

#ifdef XP_WIN
using PidType = DWORD;
constexpr bool IsValidPid(long long pid) {
  // Excluding `(DWORD)-1` because it is not a valid process ID.
  // See https://devblogs.microsoft.com/oldnewthing/20040223-00/?p=40503
  return pid > 0 && pid < std::numeric_limits<PidType>::max();
}
#else
using PidType = pid_t;
constexpr bool IsValidPid(long long pid) {
  return pid > 0 && pid <= std::numeric_limits<PidType>::max();
}
#endif

PidType ConvertPid(const char* pidStr) {
  long long pid = strtoll(pidStr, nullptr, 10);
  if (!IsValidPid(pid)) {
    return 0;
  }
  return static_cast<PidType>(pid);
}

int StartServer(int argc, char* argv[], SSLSNISocketConfig sniSocketConfig,
                void* sniSocketConfigArg, ServerConfigFunc configFunc,
                ConnectionHandlerFunc connectionHandler) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <NSS DB directory> <ppid>\n", argv[0]);
    return 1;
  }
  const char* nssCertDBDir = argv[1];
  PidType ppid = ConvertPid(argv[2]);

  const char* debugLevel = PR_GetEnv("MOZ_TLS_SERVER_DEBUG_LEVEL");
  if (debugLevel) {
    int level = atoi(debugLevel);
    switch (level) {
      case DEBUG_ERRORS:
        gDebugLevel = DEBUG_ERRORS;
        break;
      case DEBUG_WARNINGS:
        gDebugLevel = DEBUG_WARNINGS;
        break;
      case DEBUG_VERBOSE:
        gDebugLevel = DEBUG_VERBOSE;
        break;
      default:
        PrintPRError("invalid MOZ_TLS_SERVER_DEBUG_LEVEL");
        return 1;
    }
  }

  const char* callbackPort = PR_GetEnv("MOZ_TLS_SERVER_CALLBACK_PORT");
  if (callbackPort) {
    gCallbackPort = atoi(callbackPort);
  }

  if (InitializeNSS(nssCertDBDir) != SECSuccess) {
    PR_fprintf(PR_STDERR, "InitializeNSS failed");
    return 1;
  }

  if (NSS_SetDomesticPolicy() != SECSuccess) {
    PrintPRError("NSS_SetDomesticPolicy failed");
    return 1;
  }

  /* Disabling NSS_KEY_SIZE_POLICY as we operate with short keys. */
  NSS_OptionSet(NSS_KEY_SIZE_POLICY_FLAGS, 0);

  if (SSL_ConfigServerSessionIDCache(0, 0, 0, nullptr) != SECSuccess) {
    PrintPRError("SSL_ConfigServerSessionIDCache failed");
    return 1;
  }

  UniquePRFileDesc serverSocket(PR_NewTCPSocket());
  if (!serverSocket) {
    PrintPRError("PR_NewTCPSocket failed");
    return 1;
  }

  PRSocketOptionData socketOption;
  socketOption.option = PR_SockOpt_Reuseaddr;
  socketOption.value.reuse_addr = true;
  PR_SetSocketOption(serverSocket.get(), &socketOption);

  PRNetAddr serverAddr;
  PR_InitializeNetAddr(PR_IpAddrLoopback, LISTEN_PORT, &serverAddr);
  if (PR_Bind(serverSocket.get(), &serverAddr) != PR_SUCCESS) {
    PrintPRError("PR_Bind failed");
    return 1;
  }

  // Backlog of 1 is enough for tests that strictly open one conn at a
  // time, but the HE 0-RTT race tests drive several overlapping TCP
  // connects via a reverse proxy — with backlog=1 macOS silently
  // drops the extras and the proxy retransmits the SYN, arriving
  // after the server has already moved its TLS state forward on the
  // accepted conn. A modest backlog is enough to absorb the race.
  if (PR_Listen(serverSocket.get(), 32) != PR_SUCCESS) {
    PrintPRError("PR_Listen failed");
    return 1;
  }

  UniquePRFileDesc rawModelSocket(PR_NewTCPSocket());
  if (!rawModelSocket) {
    PrintPRError("PR_NewTCPSocket failed for rawModelSocket");
    return 1;
  }

  UniquePRFileDesc modelSocket(SSL_ImportFD(nullptr, rawModelSocket.release()));
  if (!modelSocket) {
    PrintPRError("SSL_ImportFD of rawModelSocket failed");
    return 1;
  }

  SSLVersionRange range = {0, 0};
  if (SSL_VersionRangeGet(modelSocket.get(), &range) != SECSuccess) {
    PrintPRError("SSL_VersionRangeGet failed");
    return 1;
  }

  if (range.max < SSL_LIBRARY_VERSION_TLS_1_3) {
    range.max = SSL_LIBRARY_VERSION_TLS_1_3;
    if (SSL_VersionRangeSet(modelSocket.get(), &range) != SECSuccess) {
      PrintPRError("SSL_VersionRangeSet failed");
      return 1;
    }
  }

  if (PR_GetEnv("MOZ_TLS_SERVER_0RTT")) {
    if (SSL_CreateAntiReplayContext(PR_Now(), 1L * PR_USEC_PER_SEC, 7, 14,
                                    &antiReplay) != SECSuccess) {
      PrintPRError("Unable to create anti-replay context for 0-RTT.");
      return 1;
    }
  }

  if (SSL_SNISocketConfigHook(modelSocket.get(), sniSocketConfig,
                              sniSocketConfigArg) != SECSuccess) {
    PrintPRError("SSL_SNISocketConfigHook failed");
    return 1;
  }

  // We have to configure the server with a certificate, but it's not one
  // we're actually going to end up using. In the SNI callback, we pick
  // the right certificate for the connection.
  //
  // Provide an empty |extra_data| to force config via SSL_ConfigServerCert.
  // This is a temporary mechanism to work around inconsistent setting of
  // |authType| in the deprecated API (preventing the default cert from
  // being removed in favor of the SNI-selected cert). This may be removed
  // after Bug 1569222 removes the deprecated mechanism.
  SSLExtraServerCertData extra_data = {ssl_auth_null, nullptr, nullptr,
                                       nullptr,       nullptr, nullptr};
  if (ConfigSecureServerWithNamedCert(modelSocket.get(), DEFAULT_CERT_NICKNAME,
                                      nullptr, nullptr,
                                      &extra_data) != SECSuccess) {
    return 1;
  }

  // Call back to implementation-defined configuration func, if provided.
  if (configFunc) {
    if (((configFunc)(modelSocket.get())) != SECSuccess) {
      PrintPRError("configFunc failed");
      return 1;
    }
  }

  if (gCallbackPort != 0) {
    if (DoCallback()) {
      return 1;
    }
  }

  std::thread([ppid] {
    if (!ppid) {
      if (gDebugLevel >= DEBUG_ERRORS) {
        fprintf(stderr, "invalid ppid\n");
      }
      return;
    }
#ifdef XP_WIN
    HANDLE parent = OpenProcess(SYNCHRONIZE, false, ppid);
    if (!parent) {
      if (gDebugLevel >= DEBUG_ERRORS) {
        fprintf(stderr, "OpenProcess failed\n");
      }
      return;
    }
    WaitForSingleObject(parent, INFINITE);
    CloseHandle(parent);
#else
    while (getppid() == ppid) {
      sleep(1);
    }
#endif
    if (gDebugLevel >= DEBUG_ERRORS) {
      fprintf(stderr, "Parent process crashed\n");
    }
    exit(1);
  }).detach();

  while (true) {
    PRNetAddr clientAddr;
    PRFileDesc* clientSocket =
        PR_Accept(serverSocket.get(), &clientAddr, PR_INTERVAL_NO_TIMEOUT);
    if (connectionHandler) {
      connectionHandler(clientSocket, modelSocket);
    } else {
      HandleConnection(clientSocket, modelSocket);
    }
  }
}

}  // namespace test
}  // namespace mozilla
