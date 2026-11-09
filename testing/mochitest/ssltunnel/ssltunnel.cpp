/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * WARNING: DO NOT USE THIS CODE IN PRODUCTION SYSTEMS.  It is highly likely to
 *          be plagued with the usual problems endemic to C (buffer overflows
 *          and the like).  We don't especially care here (but would accept
 *          patches!) because this is only intended for use in our test
 *          harnesses in controlled situations where input is guaranteed not to
 *          be malicious.
 */

#include "ScopedNSSTypes.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <stdarg.h>
#include "prinit.h"
#include "prerror.h"
#include "prenv.h"
#include "prnetdb.h"
#include "prthread.h"
#include "nss.h"
#include "keyhi.h"
#include "ssl.h"
#include "sslproto.h"
#include "plhash.h"
#include "mozilla/Sprintf.h"

using namespace mozilla;
using namespace mozilla::psm;
using std::string;
using std::vector;

#define IS_DELIM(m, c) ((m)[(c) >> 3] & (1 << ((c) & 7)))
#define SET_DELIM(m, c) ((m)[(c) >> 3] |= (1 << ((c) & 7)))
#define DELIM_TABLE_SIZE 32

// You can set the level of logging by env var SSLTUNNEL_LOG_LEVEL=n, where n
// is 0 through 3.  The default is 1, INFO level logging.
enum LogLevel {
  LEVEL_DEBUG = 0,
  LEVEL_INFO = 1,
  LEVEL_ERROR = 2,
  LEVEL_SILENT = 3
} gLogLevel,
    gLastLogLevel;

#define _LOG_OUTPUT(level, func, params) \
  PR_BEGIN_MACRO                         \
  if (level >= gLogLevel) {              \
    gLastLogLevel = level;               \
    func params;                         \
  }                                      \
  PR_END_MACRO

// The most verbose output
#define LOG_DEBUG(params) _LOG_OUTPUT(LEVEL_DEBUG, printf, params)

// Top level informative messages
#define LOG_INFO(params) _LOG_OUTPUT(LEVEL_INFO, printf, params)

// Serious errors that must be logged always until completely gag
#define LOG_ERROR(params) _LOG_OUTPUT(LEVEL_ERROR, eprintf, params)

// Same as LOG_ERROR, but when logging is set to LEVEL_DEBUG, the message
// will be put to the stdout instead of stderr to keep continuity with other
// LOG_DEBUG message output
#define LOG_ERRORD(params)                     \
  PR_BEGIN_MACRO                               \
  if (gLogLevel == LEVEL_DEBUG)                \
    _LOG_OUTPUT(LEVEL_ERROR, printf, params);  \
  else                                         \
    _LOG_OUTPUT(LEVEL_ERROR, eprintf, params); \
  PR_END_MACRO

// If there is any output written between LOG_BEGIN_BLOCK() and
// LOG_END_BLOCK() then a new line will be put to the proper output (out/err)
#define LOG_BEGIN_BLOCK() gLastLogLevel = LEVEL_SILENT;

#define LOG_END_BLOCK()                                                        \
  PR_BEGIN_MACRO                                                               \
  if (gLastLogLevel == LEVEL_ERROR) LOG_ERROR(("\n"));                         \
  if (gLastLogLevel < LEVEL_ERROR) _LOG_OUTPUT(gLastLogLevel, printf, ("\n")); \
  PR_END_MACRO

int eprintf(const char* str, ...) {
  va_list ap;
  va_start(ap, str);
  int result = vfprintf(stderr, str, ap);
  va_end(ap);
  return result;
}

// Copied from nsCRT
char* strtok2(char* string, const char* delims, char** newStr) {
  PR_ASSERT(string);

  char delimTable[DELIM_TABLE_SIZE];
  uint32_t i;
  char* result;
  char* str = string;

  for (i = 0; i < DELIM_TABLE_SIZE; i++) delimTable[i] = '\0';

  for (i = 0; delims[i]; i++) {
    SET_DELIM(delimTable, static_cast<uint8_t>(delims[i]));
  }

  // skip to beginning
  while (*str && IS_DELIM(delimTable, static_cast<uint8_t>(*str))) {
    str++;
  }
  result = str;

  // fix up the end of the token
  while (*str) {
    if (IS_DELIM(delimTable, static_cast<uint8_t>(*str))) {
      *str++ = '\0';
      break;
    }
    str++;
  }
  *newStr = str;

  return str == result ? nullptr : result;
}

enum client_auth_option { caNone = 0, caRequire = 1, caRequest = 2 };

// Structs for passing data into jobs on the thread pool
struct server_info_t {
  int32_t listen_port;
  string cert_nickname;
  PLHashTable* host_cert_table;
  PLHashTable* host_clientauth_table;
  PLHashTable* host_redir_table;
  PLHashTable* host_ssl3_table;
  PLHashTable* host_tls1_table;
  PLHashTable* host_tls11_table;
  PLHashTable* host_tls12_table;
  PLHashTable* host_tls13_table;
  PLHashTable* host_3des_table;
  PLHashTable* host_failhandshake_table;
};

struct connection_info_t {
  PRFileDesc* client_sock;
  PRNetAddr client_addr;
  server_info_t* server_info;
  // the original host in the Host: header for this connection is
  // stored here, for proxied connections
  string original_host;
  // true if no SSL should be used for this connection
  bool http_proxy_only;
  // true if this connection is for a WebSocket
  bool iswebsocket;
};

struct server_match_t {
  string fullHost;
  bool matched;
};

const int32_t BUF_SIZE = 16384;
const int32_t BUF_MARGIN = 1024;
const int32_t BUF_TOTAL = BUF_SIZE + BUF_MARGIN;

struct relayBuffer {
  char *buffer, *bufferhead, *buffertail, *bufferend;

  relayBuffer() {
    // Leave 1024 bytes more for request line manipulations
    bufferhead = buffertail = buffer = new char[BUF_TOTAL];
    bufferend = buffer + BUF_SIZE;
  }

  ~relayBuffer() { delete[] buffer; }

  void compact() {
    if (buffertail == bufferhead) buffertail = bufferhead = buffer;
  }

  bool empty() { return bufferhead == buffertail; }
  size_t areafree() { return bufferend - buffertail; }
  size_t margin() { return areafree() + BUF_MARGIN; }
  size_t present() { return buffertail - bufferhead; }
};

const uint32_t DEFAULT_STACKSIZE = (512 * 1024);

// Replaces NSPR's PRThreadPool, removed from the build. Every job is
// long-lived - one per listening server, one per connection - so a thread per
// job is equivalent. Unjoinable on purpose: StartServer parks in PR_Accept and
// only re-checks shutdown_server at the top of its loop, so waiting on one can
// block forever.
static bool QueueJob(void (*aEntry)(void*), void* aArg) {
  PRThread* thread = PR_CreateThread(PR_USER_THREAD, aEntry, aArg,
                                     PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                     PR_UNJOINABLE_THREAD, DEFAULT_STACKSIZE);
  return thread != nullptr;
}

// global data
MOZ_GLIBCXX_CONSTINIT string nssconfigdir;
MOZ_GLIBCXX_CONSTINIT vector<server_info_t> servers;
PRNetAddr remote_addr;
PRNetAddr websocket_server;
PRLock* shutdown_lock = nullptr;
PRCondVar* shutdown_condvar = nullptr;
// Not really used, unless something fails to start
bool shutdown_server = false;
// Predicate for shutdown_condvar, guarded by shutdown_lock. Without it a
// SignalShutdown that runs before main reaches PR_WaitCondVar is lost and main
// blocks forever.
bool shutdown_requested = false;
bool do_http_proxy = false;
bool any_host_spec_config = false;
bool listen_public = false;

int ClientAuthValueComparator(const void* v1, const void* v2) {
  int a = *static_cast<const client_auth_option*>(v1) -
          *static_cast<const client_auth_option*>(v2);
  if (a == 0) return 0;
  if (a > 0) return 1;
  // (a < 0)
  return -1;
}

static int match_hostname(PLHashEntry* he, int /*index*/, void* arg) {
  server_match_t* match = (server_match_t*)arg;
  if (match->fullHost.find((char*)he->key) != string::npos)
    match->matched = true;
  return HT_ENUMERATE_NEXT;
}

/*
 * Signal the main thread that the application should shut down.
 */
void SignalShutdown() {
  PR_Lock(shutdown_lock);
  shutdown_requested = true;
  PR_NotifyCondVar(shutdown_condvar);
  PR_Unlock(shutdown_lock);
}

// available flags
enum {
  USE_SSL3 = 1 << 0,
  USE_3DES = 1 << 1,
  FAIL_HANDSHAKE = 1 << 2,
  USE_TLS1 = 1 << 3,
  USE_TLS1_1 = 1 << 4,
  USE_TLS1_2 = 1 << 5,
  USE_TLS1_3 = 1 << 6
};

bool ReadConnectRequest(server_info_t* server_info, relayBuffer& buffer,
                        int32_t* result, string& certificate,
                        client_auth_option* clientauth, string& host,
                        string& location, int32_t* flags) {
  if (buffer.present() < 4) {
    LOG_DEBUG(
        (" !! only %d bytes present in the buffer", (int)buffer.present()));
    return false;
  }
  if (strncmp(buffer.buffertail - 4, "\r\n\r\n", 4)) {
    LOG_ERRORD((" !! request is not tailed with CRLFCRLF but with %x %x %x %x",
                *(buffer.buffertail - 4), *(buffer.buffertail - 3),
                *(buffer.buffertail - 2), *(buffer.buffertail - 1)));
    return false;
  }

  LOG_DEBUG((" parsing initial connect request, dump:\n%.*s\n",
             (int)buffer.present(), buffer.bufferhead));

  *result = 400;

  char* token;
  char* _caret;
  token = strtok2(buffer.bufferhead, " ", &_caret);
  if (!token) {
    LOG_ERRORD((" no space found"));
    return true;
  }
  if (strcmp(token, "CONNECT")) {
    LOG_ERRORD((" not CONNECT request but %s", token));
    return true;
  }

  token = strtok2(_caret, " ", &_caret);
  void* c = PL_HashTableLookup(server_info->host_cert_table, token);
  if (c) certificate = static_cast<char*>(c);

  host = "https://";
  host += token;

  c = PL_HashTableLookup(server_info->host_clientauth_table, token);
  if (c)
    *clientauth = *static_cast<client_auth_option*>(c);
  else
    *clientauth = caNone;

  void* redir = PL_HashTableLookup(server_info->host_redir_table, token);
  if (redir) location = static_cast<char*>(redir);

  if (PL_HashTableLookup(server_info->host_ssl3_table, token)) {
    *flags |= USE_SSL3;
  }

  if (PL_HashTableLookup(server_info->host_3des_table, token)) {
    *flags |= USE_3DES;
  }

  if (PL_HashTableLookup(server_info->host_tls1_table, token)) {
    *flags |= USE_TLS1;
  }

  if (PL_HashTableLookup(server_info->host_tls11_table, token)) {
    *flags |= USE_TLS1_1;
  }

  if (PL_HashTableLookup(server_info->host_tls12_table, token)) {
    *flags |= USE_TLS1_2;
  }

  if (PL_HashTableLookup(server_info->host_tls13_table, token)) {
    *flags |= USE_TLS1_3;
  }

  if (PL_HashTableLookup(server_info->host_failhandshake_table, token)) {
    *flags |= FAIL_HANDSHAKE;
  }

  token = strtok2(_caret, "/", &_caret);
  if (strcmp(token, "HTTP")) {
    LOG_ERRORD((" not tailed with HTTP but with %s", token));
    return true;
  }

  *result = (redir) ? 302 : 200;
  return true;
}

bool ConfigureSSLServerSocket(PRFileDesc* socket, server_info_t* si,
                              const string& certificate,
                              const client_auth_option clientAuth,
                              int32_t flags) {
  const char* certnick =
      certificate.empty() ? si->cert_nickname.c_str() : certificate.c_str();

  UniqueCERTCertificate cert(PK11_FindCertFromNickname(certnick, nullptr));
  if (!cert) {
    LOG_ERROR(("Failed to find cert %s\n", certnick));
    return false;
  }

  UniqueSECKEYPrivateKey privKey(PK11_FindKeyByAnyCert(cert.get(), nullptr));
  if (!privKey) {
    LOG_ERROR(("Failed to find private key\n"));
    return false;
  }

  PRFileDesc* ssl_socket = SSL_ImportFD(nullptr, socket);
  if (!ssl_socket) {
    LOG_ERROR(("Error importing SSL socket\n"));
    return false;
  }

  if (flags & FAIL_HANDSHAKE) {
    // deliberately cause handshake to fail by sending the client a client hello
    SSL_ResetHandshake(ssl_socket, false);
    return true;
  }

  SSLKEAType certKEA = NSS_FindCertKEAType(cert.get());
  if (SSL_ConfigSecureServer(ssl_socket, cert.get(), privKey.get(), certKEA) !=
      SECSuccess) {
    LOG_ERROR(("Error configuring SSL server socket\n"));
    return false;
  }

  SSL_OptionSet(ssl_socket, SSL_SECURITY, true);
  SSL_OptionSet(ssl_socket, SSL_HANDSHAKE_AS_CLIENT, false);
  SSL_OptionSet(ssl_socket, SSL_HANDSHAKE_AS_SERVER, true);
  SSL_OptionSet(ssl_socket, SSL_ENABLE_SESSION_TICKETS, true);

  if (clientAuth != caNone) {
    // If we're requesting or requiring a client certificate, we should
    // configure NSS to include the "certificate_authorities" field in the
    // certificate request message. That way we can test that gecko properly
    // takes note of it.
    UniqueCERTCertificate issuer(
        CERT_FindCertIssuer(cert.get(), PR_Now(), certUsageAnyCA));
    if (!issuer) {
      LOG_DEBUG(("Failed to find issuer for %s\n", certnick));
      return false;
    }
    UniqueCERTCertList issuerList(CERT_NewCertList());
    if (!issuerList) {
      LOG_ERROR(("Failed to allocate new CERTCertList\n"));
      return false;
    }
    if (CERT_AddCertToListTail(issuerList.get(), issuer.get()) != SECSuccess) {
      LOG_ERROR(("Failed to add issuer to issuerList\n"));
      return false;
    }
    (void)issuer.release();  // Ownership transferred to issuerList.
    if (SSL_SetTrustAnchors(ssl_socket, issuerList.get()) != SECSuccess) {
      LOG_ERROR(
          ("Failed to set certificate_authorities list for client "
           "authentication\n"));
      return false;
    }
    SSL_OptionSet(ssl_socket, SSL_REQUEST_CERTIFICATE, true);
    SSL_OptionSet(ssl_socket, SSL_REQUIRE_CERTIFICATE, clientAuth == caRequire);
  }

  SSLVersionRange range = {SSL_LIBRARY_VERSION_TLS_1_3,
                           SSL_LIBRARY_VERSION_3_0};
  if (flags & USE_SSL3) {
    range.min = PR_MIN(range.min, SSL_LIBRARY_VERSION_3_0);
    range.max = PR_MAX(range.max, SSL_LIBRARY_VERSION_3_0);
  }
  if (flags & USE_TLS1) {
    range.min = PR_MIN(range.min, SSL_LIBRARY_VERSION_TLS_1_0);
    range.max = PR_MAX(range.max, SSL_LIBRARY_VERSION_TLS_1_0);
  }
  if (flags & USE_TLS1_1) {
    range.min = PR_MIN(range.min, SSL_LIBRARY_VERSION_TLS_1_1);
    range.max = PR_MAX(range.max, SSL_LIBRARY_VERSION_TLS_1_1);
  }
  if (flags & USE_TLS1_2) {
    range.min = PR_MIN(range.min, SSL_LIBRARY_VERSION_TLS_1_2);
    range.max = PR_MAX(range.max, SSL_LIBRARY_VERSION_TLS_1_2);
  }
  if (flags & USE_TLS1_3) {
    range.min = PR_MIN(range.min, SSL_LIBRARY_VERSION_TLS_1_3);
    range.max = PR_MAX(range.max, SSL_LIBRARY_VERSION_TLS_1_3);
  }
  // Set the valid range, if any were specified (if not, skip
  // when the default range is invalid, i.e. max > min)
  if (range.min <= range.max &&
      SSL_VersionRangeSet(ssl_socket, &range) != SECSuccess) {
    LOG_ERROR(("Error configuring SSL socket version range\n"));
    return false;
  }

  if (flags & USE_3DES) {
    for (uint16_t i = 0; i < SSL_NumImplementedCiphers; ++i) {
      uint16_t cipher_id = SSL_ImplementedCiphers[i];
      if (cipher_id == TLS_RSA_WITH_3DES_EDE_CBC_SHA) {
        SSL_CipherPrefSet(ssl_socket, cipher_id, true);
      } else {
        SSL_CipherPrefSet(ssl_socket, cipher_id, false);
      }
    }
  }

  SSL_ResetHandshake(ssl_socket, true);

  return true;
}

/**
 * This function examines the buffer for a Sec-WebSocket-Location: field,
 * and if it's present, it replaces the hostname in that field with the
 * value in the server's original_host field.  This function works
 * in the reverse direction as AdjustWebSocketHost(), replacing the real
 * hostname of a response with the potentially fake hostname that is expected
 * by the browser (e.g., mochi.test).
 *
 * @return true if the header was adjusted successfully, or not found, false
 * if the header is present but the url is not, which should indicate
 * that more data needs to be read from the socket
 */
bool AdjustWebSocketLocation(relayBuffer& buffer, connection_info_t* ci) {
  assert(buffer.margin());
  buffer.buffertail[1] = '\0';

  char* wsloc_header = strstr(buffer.bufferhead, "Sec-WebSocket-Location:");
  if (!wsloc_header) {
    return true;
  }
  // advance pointer to the start of the hostname
  char* wsloc = strstr(wsloc_header, "ws://");
  if (!wsloc) {
    wsloc = strstr(wsloc_header, "wss://");
  }
  if (!wsloc) return false;
  wsloc += 5;
  // find the end of the hostname
  char* wslocend = strchr(wsloc + 1, '/');
  if (!wslocend) return false;
  char* crlf = strstr(wsloc, "\r\n");
  if (!crlf) return false;
  if (ci->original_host.empty()) return true;

  int diff = ci->original_host.length() - (wslocend - wsloc);
  if (diff > 0) assert(size_t(diff) <= buffer.margin());
  memmove(wslocend + diff, wslocend, buffer.buffertail - wsloc - diff);
  buffer.buffertail += diff;

  memcpy(wsloc, ci->original_host.c_str(), ci->original_host.length());
  return true;
}

/**
 * This function examines the buffer for a Host: field, and if it's present,
 * it replaces the hostname in that field with the hostname in the server's
 * remote_addr field.  This is needed because proxy requests may be coming
 * from mochitest with fake hosts, like mochi.test, and these need to be
 * replaced with the host that the destination server is actually running
 * on.
 */
bool AdjustWebSocketHost(relayBuffer& buffer, connection_info_t* ci) {
  const char HEADER_UPGRADE[] = "Upgrade:";
  const char HEADER_HOST[] = "Host:";

  PRNetAddr inet_addr =
      (websocket_server.inet.port ? websocket_server : remote_addr);

  assert(buffer.margin());

  // Cannot use strnchr so add a null char at the end. There is always some
  // space left because we preserve a margin.
  buffer.buffertail[1] = '\0';

  // Verify this is a WebSocket header.
  char* h1 = strstr(buffer.bufferhead, HEADER_UPGRADE);
  if (!h1) return false;
  h1 += strlen(HEADER_UPGRADE);
  h1 += strspn(h1, " \t");
  char* h2 = strstr(h1, "WebSocket\r\n");
  if (!h2) h2 = strstr(h1, "websocket\r\n");
  if (!h2) h2 = strstr(h1, "Websocket\r\n");
  if (!h2) return false;

  char* host = strstr(buffer.bufferhead, HEADER_HOST);
  if (!host) return false;
  // advance pointer to beginning of hostname
  host += strlen(HEADER_HOST);
  host += strspn(host, " \t");

  char* endhost = strstr(host, "\r\n");
  if (!endhost) return false;

  // Save the original host, so we can use it later on responses from the
  // server.
  ci->original_host.assign(host, endhost - host);

  char newhost[40];
  PR_NetAddrToString(&inet_addr, newhost, sizeof(newhost));
  assert(strlen(newhost) < sizeof(newhost) - 7);
  SprintfLiteral(newhost, "%s:%d", newhost, PR_ntohs(inet_addr.inet.port));

  int diff = strlen(newhost) - (endhost - host);
  if (diff > 0) assert(size_t(diff) <= buffer.margin());
  memmove(endhost + diff, endhost, buffer.buffertail - host - diff);
  buffer.buffertail += diff;

  memcpy(host, newhost, strlen(newhost));
  return true;
}

/**
 * This function prefixes Request-URI path with a full scheme-host-port
 * string.
 */
bool AdjustRequestURI(relayBuffer& buffer, string* host) {
  assert(buffer.margin());

  // Cannot use strnchr so add a null char at the end. There is always some
  // space left because we preserve a margin.
  buffer.buffertail[1] = '\0';
  LOG_DEBUG((" incoming request to adjust:\n%s\n", buffer.bufferhead));

  char *token, *path;
  path = strchr(buffer.bufferhead, ' ') + 1;
  if (!path) return false;

  // If the path doesn't start with a slash don't change it, it is probably '*'
  // or a full path already. Return true, we are done with this request
  // adjustment.
  if (*path != '/') return true;

  token = strchr(path, ' ') + 1;
  if (!token) return false;

  if (strncmp(token, "HTTP/", 5)) return false;

  size_t hostlength = host->length();
  assert(hostlength <= buffer.margin());

  memmove(path + hostlength, path, buffer.buffertail - path);
  memcpy(path, host->c_str(), hostlength);
  buffer.buffertail += hostlength;

  return true;
}

bool ConnectSocket(UniquePRFileDesc& fd, const PRNetAddr* addr,
                   PRIntervalTime timeout) {
  PRStatus stat = PR_Connect(fd.get(), addr, timeout);
  if (stat != PR_SUCCESS) return false;

  PRSocketOptionData option;
  option.option = PR_SockOpt_Nonblocking;
  option.value.non_blocking = true;
  PR_SetSocketOption(fd.get(), &option);

  return true;
}

/*
 * Handle an incoming client connection. The server thread has already
 * accepted the connection, so we just need to connect to the remote
 * port and then proxy data back and forth.
 * The data parameter is a connection_info_t*, and must be deleted
 * by this function.
 */
void HandleConnection(void* data) {
  connection_info_t* ci = static_cast<connection_info_t*>(data);
  PRIntervalTime connect_timeout = PR_SecondsToInterval(30);

  UniquePRFileDesc other_sock(PR_NewTCPSocket());
  bool client_done = false;
  bool client_error = false;
  bool connect_accepted = !do_http_proxy;
  bool ssl_updated = !do_http_proxy;
  bool expect_request_start = do_http_proxy;
  string certificateToUse;
  string locationHeader;
  client_auth_option clientAuth;
  string fullHost;
  int32_t flags = 0;

  LOG_DEBUG(("SSLTUNNEL(%p)): incoming connection csock(0)=%p, ssock(1)=%p\n",
             static_cast<void*>(data), static_cast<void*>(ci->client_sock),
             static_cast<void*>(other_sock.get())));
  if (other_sock) {
    int32_t numberOfSockets = 1;

    relayBuffer buffers[2];

    if (!do_http_proxy) {
      if (!ConfigureSSLServerSocket(ci->client_sock, ci->server_info,
                                    certificateToUse, caNone, flags))
        client_error = true;
      else if (!ConnectSocket(other_sock, &remote_addr, connect_timeout))
        client_error = true;
      else
        numberOfSockets = 2;
    }

    PRPollDesc sockets[2] = {{ci->client_sock, PR_POLL_READ, 0},
                             {other_sock.get(), PR_POLL_READ, 0}};
    bool socketErrorState[2] = {false, false};

    while (!((client_error || client_done) && buffers[0].empty() &&
             buffers[1].empty())) {
      sockets[0].in_flags |= PR_POLL_EXCEPT;
      sockets[1].in_flags |= PR_POLL_EXCEPT;
      LOG_DEBUG(("SSLTUNNEL(%p)): polling flags csock(0)=%c%c, ssock(1)=%c%c\n",
                 static_cast<void*>(data),
                 sockets[0].in_flags & PR_POLL_READ ? 'R' : '-',
                 sockets[0].in_flags & PR_POLL_WRITE ? 'W' : '-',
                 sockets[1].in_flags & PR_POLL_READ ? 'R' : '-',
                 sockets[1].in_flags & PR_POLL_WRITE ? 'W' : '-'));
      int32_t pollStatus =
          PR_Poll(sockets, numberOfSockets, PR_MillisecondsToInterval(1000));
      if (pollStatus < 0) {
        LOG_DEBUG(("SSLTUNNEL(%p)): pollStatus=%d, exiting\n",
                   static_cast<void*>(data), pollStatus));
        client_error = true;
        break;
      }

      if (pollStatus == 0) {
        // timeout
        LOG_DEBUG(("SSLTUNNEL(%p)): poll timeout, looping\n",
                   static_cast<void*>(data)));
        continue;
      }

      for (int32_t s = 0; s < numberOfSockets; ++s) {
        int32_t s2 = s == 1 ? 0 : 1;
        int16_t out_flags = sockets[s].out_flags;
        int16_t& in_flags = sockets[s].in_flags;
        int16_t& in_flags2 = sockets[s2].in_flags;
        sockets[s].out_flags = 0;

        LOG_BEGIN_BLOCK();
        LOG_DEBUG(("SSLTUNNEL(%p)): %csock(%d)=%p out_flags=%d",
                   static_cast<void*>(data), s == 0 ? 'c' : 's', s,
                   static_cast<void*>(sockets[s].fd), out_flags));
        if (out_flags & (PR_POLL_EXCEPT | PR_POLL_ERR | PR_POLL_HUP)) {
          LOG_DEBUG((" :exception\n"));
          client_error = true;
          socketErrorState[s] = true;
          // We got a fatal error state on the socket. Clear the output buffer
          // for this socket to break the main loop, we will never more be able
          // to send those data anyway.
          buffers[s2].bufferhead = buffers[s2].buffertail = buffers[s2].buffer;
          continue;
        }  // PR_POLL_EXCEPT, PR_POLL_ERR, PR_POLL_HUP handling

        if (out_flags & PR_POLL_READ && !buffers[s].areafree()) {
          LOG_DEBUG(
              (" no place in read buffer but got read flag, dropping it now!"));
          in_flags &= ~PR_POLL_READ;
        }

        if (out_flags & PR_POLL_READ && buffers[s].areafree()) {
          LOG_DEBUG((" :reading"));
          int32_t bytesRead =
              PR_Recv(sockets[s].fd, buffers[s].buffertail,
                      buffers[s].areafree(), 0, PR_INTERVAL_NO_TIMEOUT);

          if (bytesRead == 0) {
            LOG_DEBUG((" socket gracefully closed"));
            client_done = true;
            in_flags &= ~PR_POLL_READ;
          } else if (bytesRead < 0) {
            if (PR_GetError() != PR_WOULD_BLOCK_ERROR) {
              LOG_DEBUG((" error=%d", PR_GetError()));
              // We are in error state, indicate that the connection was
              // not closed gracefully
              client_error = true;
              socketErrorState[s] = true;
              // Wipe out our send buffer, we cannot send it anyway.
              buffers[s2].bufferhead = buffers[s2].buffertail =
                  buffers[s2].buffer;
            } else
              LOG_DEBUG((" would block"));
          } else {
            // If the other socket is in error state (unable to send/receive)
            // throw this data away and continue loop
            if (socketErrorState[s2]) {
              LOG_DEBUG((" have read but other socket is in error state\n"));
              continue;
            }

            buffers[s].buffertail += bytesRead;
            LOG_DEBUG((", read %d bytes", bytesRead));

            // We have to accept and handle the initial CONNECT request here
            int32_t response;
            if (!connect_accepted &&
                ReadConnectRequest(ci->server_info, buffers[s], &response,
                                   certificateToUse, &clientAuth, fullHost,
                                   locationHeader, &flags)) {
              // Mark this as a proxy-only connection (no SSL) if the CONNECT
              // request didn't come for port 443 or from any of the server's
              // cert or clientauth hostnames.
              if (fullHost.find(":443") == string::npos) {
                server_match_t match;
                match.fullHost = fullHost;
                match.matched = false;
                PL_HashTableEnumerateEntries(ci->server_info->host_cert_table,
                                             match_hostname, &match);
                PL_HashTableEnumerateEntries(
                    ci->server_info->host_clientauth_table, match_hostname,
                    &match);
                PL_HashTableEnumerateEntries(ci->server_info->host_ssl3_table,
                                             match_hostname, &match);
                PL_HashTableEnumerateEntries(ci->server_info->host_tls1_table,
                                             match_hostname, &match);
                PL_HashTableEnumerateEntries(ci->server_info->host_tls11_table,
                                             match_hostname, &match);
                PL_HashTableEnumerateEntries(ci->server_info->host_tls12_table,
                                             match_hostname, &match);
                PL_HashTableEnumerateEntries(ci->server_info->host_tls13_table,
                                             match_hostname, &match);
                PL_HashTableEnumerateEntries(ci->server_info->host_3des_table,
                                             match_hostname, &match);
                PL_HashTableEnumerateEntries(
                    ci->server_info->host_failhandshake_table, match_hostname,
                    &match);
                ci->http_proxy_only = !match.matched;
              } else {
                ci->http_proxy_only = false;
              }

              // Clean the request as it would be read
              buffers[s].bufferhead = buffers[s].buffertail = buffers[s].buffer;
              in_flags |= PR_POLL_WRITE;
              connect_accepted = true;

              // Store response to the oposite buffer
              if (response == 200) {
                LOG_DEBUG(
                    (" accepted CONNECT request, connected to the server, "
                     "sending OK to the client\n"));
                strcpy(
                    buffers[s2].buffer,
                    "HTTP/1.1 200 Connected\r\nConnection: keep-alive\r\n\r\n");
              } else if (response == 302) {
                LOG_DEBUG(
                    (" accepted CONNECT request with redirection, "
                     "sending location and 302 to the client\n"));
                client_done = true;
                snprintf(buffers[s2].buffer,
                         buffers[s2].bufferend - buffers[s2].buffer,
                         "HTTP/1.1 302 Moved\r\n"
                         "Location: https://%s/\r\n"
                         "Connection: close\r\n\r\n",
                         locationHeader.c_str());
              } else {
                LOG_ERRORD(
                    (" could not read the connect request, closing connection "
                     "with %d",
                     response));
                client_done = true;
                snprintf(buffers[s2].buffer,
                         buffers[s2].bufferend - buffers[s2].buffer,
                         "HTTP/1.1 %d ERROR\r\nConnection: close\r\n\r\n",
                         response);

                break;
              }

              buffers[s2].buffertail =
                  buffers[s2].buffer + strlen(buffers[s2].buffer);

              // Send the response to the client socket
              break;
            }  // end of CONNECT handling

            if (!buffers[s].areafree()) {
              // Do not poll for read when the buffer is full
              LOG_DEBUG((" no place in our read buffer, stop reading"));
              in_flags &= ~PR_POLL_READ;
            }

            if (ssl_updated) {
              if (s == 0 && expect_request_start) {
                if (!strstr(buffers[s].bufferhead, "\r\n\r\n")) {
                  // We haven't received the complete header yet, so wait.
                  continue;
                }
                ci->iswebsocket = AdjustWebSocketHost(buffers[s], ci);
                expect_request_start = !(
                    ci->iswebsocket || AdjustRequestURI(buffers[s], &fullHost));
                PRNetAddr* addr = &remote_addr;
                if (ci->iswebsocket && websocket_server.inet.port)
                  addr = &websocket_server;
                if (!ConnectSocket(other_sock, addr, connect_timeout)) {
                  LOG_ERRORD(
                      (" could not open connection to the real server\n"));
                  client_error = true;
                  break;
                }
                LOG_DEBUG(("\n connected to remote server\n"));
                numberOfSockets = 2;
              } else if (s == 1 && ci->iswebsocket) {
                if (!AdjustWebSocketLocation(buffers[s], ci)) continue;
              }

              in_flags2 |= PR_POLL_WRITE;
              LOG_DEBUG((" telling the other socket to write"));
            } else
              LOG_DEBUG(
                  (" we have something for the other socket to write, but ssl "
                   "has not been administered on it"));
          }
        }  // PR_POLL_READ handling

        if (out_flags & PR_POLL_WRITE) {
          LOG_DEBUG((" :writing"));
          int32_t bytesWrite =
              PR_Send(sockets[s].fd, buffers[s2].bufferhead,
                      buffers[s2].present(), 0, PR_INTERVAL_NO_TIMEOUT);

          if (bytesWrite < 0) {
            if (PR_GetError() != PR_WOULD_BLOCK_ERROR) {
              LOG_DEBUG((" error=%d", PR_GetError()));
              client_error = true;
              socketErrorState[s] = true;
              // We got a fatal error while writting the buffer. Clear it to
              // break the main loop, we will never more be able to send it.
              buffers[s2].bufferhead = buffers[s2].buffertail =
                  buffers[s2].buffer;
            } else
              LOG_DEBUG((" would block"));
          } else {
            LOG_DEBUG((", written %d bytes", bytesWrite));
            buffers[s2].buffertail[1] = '\0';
            LOG_DEBUG((" dump:\n%.*s\n", bytesWrite, buffers[s2].bufferhead));

            buffers[s2].bufferhead += bytesWrite;
            if (buffers[s2].present()) {
              LOG_DEBUG((" still have to write %d bytes",
                         (int)buffers[s2].present()));
              in_flags |= PR_POLL_WRITE;
            } else {
              if (!ssl_updated) {
                LOG_DEBUG((" proxy response sent to the client"));
                // Proxy response has just been writen, update to ssl
                ssl_updated = true;
                if (ci->http_proxy_only) {
                  LOG_DEBUG(
                      (" not updating to SSL based on http_proxy_only for this "
                       "socket"));
                } else if (!ConfigureSSLServerSocket(
                               ci->client_sock, ci->server_info,
                               certificateToUse, clientAuth, flags)) {
                  LOG_ERRORD((" failed to config server socket\n"));
                  client_error = true;
                  break;
                } else {
                  LOG_DEBUG((" client socket updated to SSL"));
                }
              }  // sslUpdate

              LOG_DEBUG(
                  (" dropping our write flag and setting other socket read "
                   "flag"));
              in_flags &= ~PR_POLL_WRITE;
              in_flags2 |= PR_POLL_READ;
              buffers[s2].compact();
            }
          }
        }  // PR_POLL_WRITE handling
        LOG_END_BLOCK();  // end the log
      }  // for...
    }  // while, poll
  } else
    client_error = true;

  LOG_DEBUG(("SSLTUNNEL(%p)): exiting root function for csock=%p, ssock=%p\n",
             static_cast<void*>(data), static_cast<void*>(ci->client_sock),
             static_cast<void*>(other_sock.get())));
  if (!client_error) PR_Shutdown(ci->client_sock, PR_SHUTDOWN_SEND);
  PR_Close(ci->client_sock);

  delete ci;
}

/*
 * Start listening for SSL connections on a specified port, handing
 * them off to client threads after accepting the connection.
 * The data parameter is a server_info_t*, owned by the calling
 * function.
 */
void StartServer(void* data) {
  server_info_t* si = static_cast<server_info_t*>(data);

  // TODO: select ciphers?
  UniquePRFileDesc listen_socket(PR_NewTCPSocket());
  if (!listen_socket) {
    LOG_ERROR(("failed to create socket\n"));
    SignalShutdown();
    return;
  }

  // In case the socket is still open in the TIME_WAIT state from a previous
  // instance of ssltunnel we ask to reuse the port.
  PRSocketOptionData socket_option;
  socket_option.option = PR_SockOpt_Reuseaddr;
  socket_option.value.reuse_addr = true;
  PR_SetSocketOption(listen_socket.get(), &socket_option);

  PRNetAddr server_addr;
  PRNetAddrValue listen_addr;
  if (listen_public) {
    listen_addr = PR_IpAddrAny;
  } else {
    listen_addr = PR_IpAddrLoopback;
  }
  PR_InitializeNetAddr(listen_addr, si->listen_port, &server_addr);

  if (PR_Bind(listen_socket.get(), &server_addr) != PR_SUCCESS) {
    LOG_ERROR(("failed to bind socket on port %d: error %d\n", si->listen_port,
               PR_GetError()));
    SignalShutdown();
    return;
  }

  // Tests can open many connections at once, and a single thread accepts them.
  // Connects that overflow the backlog are reset (macOS) or delayed by SYN
  // retransmits (Linux); resets can exhaust necko's retries. 128 is SOMAXCONN
  // on macOS.
  if (PR_Listen(listen_socket.get(), 128) != PR_SUCCESS) {
    LOG_ERROR(("failed to listen on socket\n"));
    SignalShutdown();
    return;
  }

  LOG_INFO(("Server listening on port %d with cert %s\n", si->listen_port,
            si->cert_nickname.c_str()));

  while (!shutdown_server) {
    connection_info_t* ci = new connection_info_t();
    ci->server_info = si;
    ci->http_proxy_only = do_http_proxy;
    // block waiting for connections
    ci->client_sock = PR_Accept(listen_socket.get(), &ci->client_addr,
                                PR_INTERVAL_NO_TIMEOUT);

    PRSocketOptionData option;
    option.option = PR_SockOpt_Nonblocking;
    option.value.non_blocking = true;
    PR_SetSocketOption(ci->client_sock, &option);

    if (!ci->client_sock) {
      delete ci;
    } else if (!QueueJob(HandleConnection, ci)) {
      LOG_ERROR(("Failed to start connection thread\n"));
      PR_Close(ci->client_sock);
      delete ci;
    }
  }
}

// bogus password func, just don't use passwords. :-P
char* password_func(PK11SlotInfo* /*slot*/, PRBool retry, void* /*arg*/) {
  if (retry) return nullptr;

  return PL_strdup("");
}

server_info_t* findServerInfo(int portnumber) {
  for (auto& server : servers) {
    if (server.listen_port == portnumber) return &server;
  }

  return nullptr;
}

PLHashTable* get_ssl3_table(server_info_t* server) {
  return server->host_ssl3_table;
}

PLHashTable* get_tls1_table(server_info_t* server) {
  return server->host_tls1_table;
}

PLHashTable* get_tls11_table(server_info_t* server) {
  return server->host_tls11_table;
}

PLHashTable* get_tls12_table(server_info_t* server) {
  return server->host_tls12_table;
}

PLHashTable* get_tls13_table(server_info_t* server) {
  return server->host_tls13_table;
}

PLHashTable* get_3des_table(server_info_t* server) {
  return server->host_3des_table;
}

PLHashTable* get_failhandshake_table(server_info_t* server) {
  return server->host_failhandshake_table;
}

int parseWeakCryptoConfig(char* const& keyword, char*& _caret,
                          PLHashTable* (*get_table)(server_info_t*)) {
  char* hostname = strtok2(_caret, ":", &_caret);
  char* hostportstring = strtok2(_caret, ":", &_caret);
  char* serverportstring = strtok2(_caret, "\n", &_caret);

  int port = atoi(serverportstring);
  if (port <= 0) {
    LOG_ERROR(("Invalid port specified: %s\n", serverportstring));
    return 1;
  }

  if (server_info_t* existingServer = findServerInfo(port)) {
    any_host_spec_config = true;

    char* hostname_copy =
        new char[strlen(hostname) + strlen(hostportstring) + 2];
    if (!hostname_copy) {
      LOG_ERROR(("Out of memory"));
      return 1;
    }

    strcpy(hostname_copy, hostname);
    strcat(hostname_copy, ":");
    strcat(hostname_copy, hostportstring);

    PLHashEntry* entry =
        PL_HashTableAdd(get_table(existingServer), hostname_copy, keyword);
    if (!entry) {
      LOG_ERROR(("Out of memory"));
      return 1;
    }
  } else {
    LOG_ERROR(
        ("Server on port %d for redirhost option is not defined, use 'listen' "
         "option first",
         port));
    return 1;
  }

  return 0;
}

int processConfigLine(char* configLine) {
  if (*configLine == 0 || *configLine == '#') return 0;

  char* _caret;
  char* keyword = strtok2(configLine, ":", &_caret);
  // Configure usage of http/ssl tunneling proxy behavior
  if (!strcmp(keyword, "httpproxy")) {
    char* value = strtok2(_caret, ":", &_caret);
    if (!strcmp(value, "1")) do_http_proxy = true;

    return 0;
  }

  if (!strcmp(keyword, "websocketserver")) {
    char* ipstring = strtok2(_caret, ":", &_caret);
    if (PR_StringToNetAddr(ipstring, &websocket_server) != PR_SUCCESS) {
      LOG_ERROR(("Invalid IP address in proxy config: %s\n", ipstring));
      return 1;
    }
    char* remoteport = strtok2(_caret, ":", &_caret);
    int port = atoi(remoteport);
    if (port <= 0) {
      LOG_ERROR(("Invalid remote port in proxy config: %s\n", remoteport));
      return 1;
    }
    websocket_server.inet.port = PR_htons(port);
    return 0;
  }

  // Configure the forward address of the target server
  if (!strcmp(keyword, "forward")) {
    char* ipstring = strtok2(_caret, ":", &_caret);
    if (PR_StringToNetAddr(ipstring, &remote_addr) != PR_SUCCESS) {
      LOG_ERROR(("Invalid remote IP address: %s\n", ipstring));
      return 1;
    }
    char* serverportstring = strtok2(_caret, ":", &_caret);
    int port = atoi(serverportstring);
    if (port <= 0) {
      LOG_ERROR(("Invalid remote port: %s\n", serverportstring));
      return 1;
    }
    remote_addr.inet.port = PR_htons(port);

    return 0;
  }

  // Configure all listen sockets and port+certificate bindings.
  // Listen on the public address if "*" was specified as the listen
  // address or listen on the loopback address if "127.0.0.1" was
  // specified. Using loopback will prevent users getting errors from
  // their firewalls about ssltunnel needing permission. A public
  // address is required when proxying ssl traffic from a physical or
  // emulated Android device since it has a different ip address from
  // the host.
  if (!strcmp(keyword, "listen")) {
    char* hostname = strtok2(_caret, ":", &_caret);
    char* hostportstring = nullptr;
    if (!strcmp(hostname, "*")) {
      listen_public = true;
    } else if (strcmp(hostname, "127.0.0.1")) {
      any_host_spec_config = true;
      hostportstring = strtok2(_caret, ":", &_caret);
    }

    char* serverportstring = strtok2(_caret, ":", &_caret);
    char* certnick = strtok2(_caret, ":", &_caret);

    int port = atoi(serverportstring);
    if (port <= 0) {
      LOG_ERROR(("Invalid port specified: %s\n", serverportstring));
      return 1;
    }

    if (server_info_t* existingServer = findServerInfo(port)) {
      if (!hostportstring) {
        LOG_ERROR(
            ("Null hostportstring specified for hostname %s\n", hostname));
        return 1;
      }
      char* certnick_copy = new char[strlen(certnick) + 1];
      char* hostname_copy =
          new char[strlen(hostname) + strlen(hostportstring) + 2];

      strcpy(hostname_copy, hostname);
      strcat(hostname_copy, ":");
      strcat(hostname_copy, hostportstring);
      strcpy(certnick_copy, certnick);

      PLHashEntry* entry = PL_HashTableAdd(existingServer->host_cert_table,
                                           hostname_copy, certnick_copy);
      if (!entry) {
        LOG_ERROR(("Out of memory"));
        return 1;
      }
    } else {
      server_info_t server;
      server.cert_nickname = certnick;
      server.listen_port = port;
      server.host_cert_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);
      if (!server.host_cert_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }
      server.host_clientauth_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          ClientAuthValueComparator, nullptr, nullptr);
      if (!server.host_clientauth_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }
      server.host_redir_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);
      if (!server.host_redir_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      server.host_ssl3_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);

      if (!server.host_ssl3_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      server.host_tls1_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);

      if (!server.host_tls1_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      server.host_tls11_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);

      if (!server.host_tls11_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      server.host_tls12_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);

      if (!server.host_tls12_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      server.host_tls13_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);

      if (!server.host_tls13_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      server.host_3des_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);
      ;
      if (!server.host_3des_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      server.host_failhandshake_table =
          PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                          PL_CompareStrings, nullptr, nullptr);
      ;
      if (!server.host_failhandshake_table) {
        LOG_ERROR(("Internal, could not create hash table\n"));
        return 1;
      }

      servers.push_back(std::move(server));
    }

    return 0;
  }

  if (!strcmp(keyword, "clientauth")) {
    char* hostname = strtok2(_caret, ":", &_caret);
    char* hostportstring = strtok2(_caret, ":", &_caret);
    char* serverportstring = strtok2(_caret, ":", &_caret);

    int port = atoi(serverportstring);
    if (port <= 0) {
      LOG_ERROR(("Invalid port specified: %s\n", serverportstring));
      return 1;
    }

    if (server_info_t* existingServer = findServerInfo(port)) {
      char* authoptionstring = strtok2(_caret, ":", &_caret);
      client_auth_option* authoption = new client_auth_option;
      if (!authoption) {
        LOG_ERROR(("Out of memory"));
        return 1;
      }

      if (!strcmp(authoptionstring, "require"))
        *authoption = caRequire;
      else if (!strcmp(authoptionstring, "request"))
        *authoption = caRequest;
      else if (!strcmp(authoptionstring, "none"))
        *authoption = caNone;
      else {
        LOG_ERROR(
            ("Incorrect client auth option modifier for host '%s'", hostname));
        delete authoption;
        return 1;
      }

      any_host_spec_config = true;

      char* hostname_copy =
          new char[strlen(hostname) + strlen(hostportstring) + 2];
      if (!hostname_copy) {
        LOG_ERROR(("Out of memory"));
        delete authoption;
        return 1;
      }

      strcpy(hostname_copy, hostname);
      strcat(hostname_copy, ":");
      strcat(hostname_copy, hostportstring);

      PLHashEntry* entry = PL_HashTableAdd(
          existingServer->host_clientauth_table, hostname_copy, authoption);
      if (!entry) {
        LOG_ERROR(("Out of memory"));
        delete authoption;
        return 1;
      }
    } else {
      LOG_ERROR(
          ("Server on port %d for client authentication option is not defined, "
           "use 'listen' option first",
           port));
      return 1;
    }

    return 0;
  }

  if (!strcmp(keyword, "redirhost")) {
    char* hostname = strtok2(_caret, ":", &_caret);
    char* hostportstring = strtok2(_caret, ":", &_caret);
    char* serverportstring = strtok2(_caret, ":", &_caret);

    int port = atoi(serverportstring);
    if (port <= 0) {
      LOG_ERROR(("Invalid port specified: %s\n", serverportstring));
      return 1;
    }

    if (server_info_t* existingServer = findServerInfo(port)) {
      char* redirhoststring = strtok2(_caret, ":", &_caret);

      any_host_spec_config = true;

      char* hostname_copy =
          new char[strlen(hostname) + strlen(hostportstring) + 2];
      if (!hostname_copy) {
        LOG_ERROR(("Out of memory"));
        return 1;
      }

      strcpy(hostname_copy, hostname);
      strcat(hostname_copy, ":");
      strcat(hostname_copy, hostportstring);

      char* redir_copy = new char[strlen(redirhoststring) + 1];
      strcpy(redir_copy, redirhoststring);
      PLHashEntry* entry = PL_HashTableAdd(existingServer->host_redir_table,
                                           hostname_copy, redir_copy);
      if (!entry) {
        LOG_ERROR(("Out of memory"));
        delete[] hostname_copy;
        delete[] redir_copy;
        return 1;
      }
    } else {
      LOG_ERROR(
          ("Server on port %d for redirhost option is not defined, use "
           "'listen' option first",
           port));
      return 1;
    }

    return 0;
  }

  if (!strcmp(keyword, "ssl3")) {
    return parseWeakCryptoConfig(keyword, _caret, get_ssl3_table);
  }
  if (!strcmp(keyword, "tls1")) {
    return parseWeakCryptoConfig(keyword, _caret, get_tls1_table);
  }
  if (!strcmp(keyword, "tls1_1")) {
    return parseWeakCryptoConfig(keyword, _caret, get_tls11_table);
  }
  if (!strcmp(keyword, "tls1_2")) {
    return parseWeakCryptoConfig(keyword, _caret, get_tls12_table);
  }
  if (!strcmp(keyword, "tls1_3")) {
    return parseWeakCryptoConfig(keyword, _caret, get_tls13_table);
  }

  if (!strcmp(keyword, "3des")) {
    return parseWeakCryptoConfig(keyword, _caret, get_3des_table);
  }

  if (!strcmp(keyword, "failHandshake")) {
    return parseWeakCryptoConfig(keyword, _caret, get_failhandshake_table);
  }

  // Configure the NSS certificate database directory
  if (!strcmp(keyword, "certdbdir")) {
    nssconfigdir = strtok2(_caret, "\n", &_caret);
    return 0;
  }

  LOG_ERROR(("Error: keyword \"%s\" unexpected\n", keyword));
  return 1;
}

int parseConfigFile(const char* filePath) {
  FILE* f = fopen(filePath, "r");
  if (!f) return 1;

  char buffer[1024], *b = buffer;
  while (!feof(f)) {
    char c;

    if (fscanf(f, "%c", &c) != 1) {
      break;
    }

    switch (c) {
      case '\n':
        *b++ = 0;
        if (processConfigLine(buffer)) {
          fclose(f);
          return 1;
        }
        b = buffer;
        continue;

      case '\r':
        continue;

      default:
        *b++ = c;
    }
  }

  fclose(f);

  // Check mandatory items
  if (nssconfigdir.empty()) {
    LOG_ERROR(
        ("Error: missing path to NSS certification database\n,use "
         "certdbdir:<path> in the config file\n"));
    return 1;
  }

  if (any_host_spec_config && !do_http_proxy) {
    LOG_ERROR(
        ("Warning: any host-specific configurations are ignored, add "
         "httpproxy:1 to allow them\n"));
  }

  return 0;
}

int freeHostCertHashItems(PLHashEntry* he, int /*i*/, void* /*arg*/) {
  delete[] (char*)he->key;
  delete[] (char*)he->value;
  return HT_ENUMERATE_REMOVE;
}

int freeHostRedirHashItems(PLHashEntry* he, int /*i*/, void* /*arg*/) {
  delete[] (char*)he->key;
  delete[] (char*)he->value;
  return HT_ENUMERATE_REMOVE;
}

int freeClientAuthHashItems(PLHashEntry* he, int /*i*/, void* /*arg*/) {
  delete[] (char*)he->key;
  delete (client_auth_option*)he->value;
  return HT_ENUMERATE_REMOVE;
}

int freeSSL3HashItems(PLHashEntry* he, int /*i*/, void* /*arg*/) {
  delete[] (char*)he->key;
  return HT_ENUMERATE_REMOVE;
}

int freeTLSHashItems(PLHashEntry* he, int /*i*/, void* /*arg*/) {
  delete[] (char*)he->key;
  return HT_ENUMERATE_REMOVE;
}

int free3DESHashItems(PLHashEntry* he, int /*i*/, void* /*arg*/) {
  delete[] (char*)he->key;
  return HT_ENUMERATE_REMOVE;
}

int main(int argc, char** argv) {
  const char* configFilePath;

  const char* logLevelEnv = PR_GetEnv("SSLTUNNEL_LOG_LEVEL");
  gLogLevel = logLevelEnv ? (LogLevel)atoi(logLevelEnv) : LEVEL_INFO;

  if (argc == 1)
    configFilePath = "ssltunnel.cfg";
  else
    configFilePath = argv[1];

  memset(&websocket_server, 0, sizeof(PRNetAddr));

  if (parseConfigFile(configFilePath)) {
    LOG_ERROR((
        "Error: config file \"%s\" missing or formating incorrect\n"
        "Specify path to the config file as parameter to ssltunnel or \n"
        "create ssltunnel.cfg in the working directory.\n\n"
        "Example format of the config file:\n\n"
        "       # Enable http/ssl tunneling proxy-like behavior.\n"
        "       # If not specified ssltunnel simply does direct forward.\n"
        "       httpproxy:1\n\n"
        "       # Specify path to the certification database used.\n"
        "       certdbdir:/path/to/certdb\n\n"
        "       # Forward/proxy all requests in raw to 127.0.0.1:8888.\n"
        "       forward:127.0.0.1:8888\n\n"
        "       # Accept connections on port 4443 or 5678 resp. and "
        "authenticate\n"
        "       # to any host ('*') using the 'server cert' or 'server cert 2' "
        "resp.\n"
        "       listen:*:4443:server cert\n"
        "       listen:*:5678:server cert 2\n\n"
        "       # Accept connections on port 4443 and authenticate using\n"
        "       # 'a different cert' when target host is 'my.host.name:443'.\n"
        "       # This only works in httpproxy mode and has higher priority\n"
        "       # than the previous option.\n"
        "       listen:my.host.name:443:4443:a different cert\n\n"
        "       # To make a specific host require or just request a client "
        "certificate\n"
        "       # to authenticate use the following options. This can only be "
        "used\n"
        "       # in httpproxy mode and only after the 'listen' option has "
        "been\n"
        "       # specified. You also have to specify the tunnel listen port.\n"
        "       clientauth:requesting-client-cert.host.com:443:4443:request\n"
        "       clientauth:requiring-client-cert.host.com:443:4443:require\n"
        "       # Proxy WebSocket traffic to the server at 127.0.0.1:9999,\n"
        "       # instead of the server specified in the 'forward' option.\n"
        "       websocketserver:127.0.0.1:9999\n",
        configFilePath));
    return 1;
  }

  shutdown_lock = PR_NewLock();
  if (!shutdown_lock) {
    LOG_ERROR(("Failed to create lock\n"));
    return 1;
  }
  shutdown_condvar = PR_NewCondVar(shutdown_lock);
  if (!shutdown_condvar) {
    LOG_ERROR(("Failed to create condvar\n"));
    PR_DestroyLock(shutdown_lock);
    return 1;
  }

  PK11_SetPasswordFunc(password_func);

  // Initialize NSS
  if (NSS_Init(nssconfigdir.c_str()) != SECSuccess) {
    int32_t errorlen = PR_GetErrorTextLength();
    if (errorlen) {
      auto err = std::make_unique<char[]>(errorlen + 1);
      PR_GetErrorText(err.get());
      LOG_ERROR(("Failed to init NSS: %s", err.get()));
    } else {
      LOG_ERROR(("Failed to init NSS: Cannot get error from NSPR."));
    }
    PR_DestroyCondVar(shutdown_condvar);
    PR_DestroyLock(shutdown_lock);
    return 1;
  }

  if (NSS_SetDomesticPolicy() != SECSuccess) {
    LOG_ERROR(("NSS_SetDomesticPolicy failed\n"));
    PR_DestroyCondVar(shutdown_condvar);
    PR_DestroyLock(shutdown_lock);
    NSS_Shutdown();
    return 1;
  }

  // these values should make NSS use the defaults
  if (SSL_ConfigServerSessionIDCache(0, 0, 0, nullptr) != SECSuccess) {
    LOG_ERROR(("SSL_ConfigServerSessionIDCache failed\n"));
    PR_DestroyCondVar(shutdown_condvar);
    PR_DestroyLock(shutdown_lock);
    NSS_Shutdown();
    return 1;
  }

  for (auto& server : servers) {
    if (!QueueJob(StartServer, &server)) {
      LOG_ERROR(("Failed to start server thread\n"));
      SignalShutdown();
      break;
    }
  }
  // now wait for someone to tell us to quit
  PR_Lock(shutdown_lock);
  while (!shutdown_requested) {
    PR_WaitCondVar(shutdown_condvar, PR_INTERVAL_NO_TIMEOUT);
  }
  PR_Unlock(shutdown_lock);
  shutdown_server = true;
  LOG_INFO(("Shutting down...\n"));
  // cleanup
  PR_DestroyCondVar(shutdown_condvar);
  PR_DestroyLock(shutdown_lock);
  if (NSS_Shutdown() == SECFailure) {
    LOG_DEBUG(("Leaked NSS objects!\n"));
  }

  for (auto& server : servers) {
    PL_HashTableEnumerateEntries(server.host_cert_table, freeHostCertHashItems,
                                 nullptr);
    PL_HashTableEnumerateEntries(server.host_clientauth_table,
                                 freeClientAuthHashItems, nullptr);
    PL_HashTableEnumerateEntries(server.host_redir_table,
                                 freeHostRedirHashItems, nullptr);
    PL_HashTableEnumerateEntries(server.host_ssl3_table, freeSSL3HashItems,
                                 nullptr);
    PL_HashTableEnumerateEntries(server.host_tls1_table, freeTLSHashItems,
                                 nullptr);
    PL_HashTableEnumerateEntries(server.host_tls11_table, freeTLSHashItems,
                                 nullptr);
    PL_HashTableEnumerateEntries(server.host_tls12_table, freeTLSHashItems,
                                 nullptr);
    PL_HashTableEnumerateEntries(server.host_tls13_table, freeTLSHashItems,
                                 nullptr);
    PL_HashTableEnumerateEntries(server.host_3des_table, free3DESHashItems,
                                 nullptr);
    PL_HashTableEnumerateEntries(server.host_failhandshake_table,
                                 free3DESHashItems, nullptr);
    PL_HashTableDestroy(server.host_cert_table);
    PL_HashTableDestroy(server.host_clientauth_table);
    PL_HashTableDestroy(server.host_redir_table);
    PL_HashTableDestroy(server.host_ssl3_table);
    PL_HashTableDestroy(server.host_tls1_table);
    PL_HashTableDestroy(server.host_tls11_table);
    PL_HashTableDestroy(server.host_tls12_table);
    PL_HashTableDestroy(server.host_tls13_table);
    PL_HashTableDestroy(server.host_3des_table);
    PL_HashTableDestroy(server.host_failhandshake_table);
  }

  PR_Cleanup();
  return 0;
}
