/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNSSIOLayer.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "NSSCertDBTrustDomain.h"
#include "NSSErrorsService.h"
#include "NSSSocketControl.h"
#include "PSMRunnable.h"
#include "SSLServerCertVerification.h"
#include "ScopedNSSTypes.h"
#include "TLSClientAuthCertSelection.h"
#include "keyhi.h"
#include "mozilla/Base64.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/RandomNum.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"
#include "mozilla/net/SSLTokensCache.h"
#include "mozilla/net/SocketProcessChild.h"
#include "mozilla/psm/IPCClientCertsChild.h"
#include "mozilla/psm/PIPCClientCertsChild.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixtypes.h"
#include "mozpkix/pkixutil.h"
#include "nsArray.h"
#include "nsArrayUtils.h"
#include "nsCRT.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsClientAuthRemember.h"
#include "nsContentUtils.h"
#include "nsISocketProvider.h"
#include "nsIWebProgressListener.h"
#include "nsNSSComponent.h"
#include "nsNSSHelper.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "prmem.h"
#include "prnetdb.h"
#include "secder.h"
#include "secerr.h"
#include "ssl.h"
#include "sslerr.h"
#include "sslexp.h"
#include "sslproto.h"
#include "zlib.h"
#include "brotli/decode.h"
#include "zstd/zstd.h"

#if defined(__arm__)
#  include "mozilla/arm.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/java/ClientAuthCertificateManagerWrappers.h"
#endif  // MOZ_WIDGET_ANDROID

using namespace mozilla;
using namespace mozilla::psm;
using namespace mozilla::ipc;

// #define DEBUG_SSL_VERBOSE //Enable this define to get minimal
//  reports when doing SSL read/write

// #define DUMP_BUFFER  //Enable this define along with
//  DEBUG_SSL_VERBOSE to dump SSL
//  read/write buffer to a log.
//  Uses PR_LOG except on Mac where
//  we always write out to our own
//  file.

namespace {

// The NSSSocketInfo tls flags are meant to be opaque to most calling
// applications but provide a mechanism for direct TLS manipulation when
// experimenting with new features in the scope of a single socket. They do not
// create a persistent ABI.
//
// Use of these flags creates a new 'sharedSSLState' so existing states for
// intolerance are not carried to sockets that use these flags (and intolerance
// they discover does not impact other normal sockets not using the flags.)
//
// Their current definitions are:
//
// bits 0-2 (mask 0x07) specify the max tls version
//          0 means no override 1->4 are 1.0, 1.1, 1.2, 1.3, 4->7 unused
// bits 3-5 (mask 0x38) specify the tls fallback limit
//          0 means no override, values 1->4 match prefs
// bit    6 (mask 0x40) was used to specify compat mode. Temporarily reserved.

enum {
  kTLSProviderFlagMaxVersion10 = 0x01,
  kTLSProviderFlagMaxVersion11 = 0x02,
  kTLSProviderFlagMaxVersion12 = 0x03,
  kTLSProviderFlagMaxVersion13 = 0x04,
};

static uint32_t getTLSProviderFlagMaxVersion(uint32_t flags) {
  return (flags & 0x07);
}

static uint32_t getTLSProviderFlagFallbackLimit(uint32_t flags) {
  return (flags & 0x38) >> 3;
}

void getSiteKey(const nsACString& hostName, uint16_t port,
                /*out*/ nsACString& key) {
  key = hostName;
  key.AppendLiteral(":");
  key.AppendInt(port);
}

}  // unnamed namespace

extern LazyLogModule gPIPNSSLog;

namespace {

enum Operation { reading, writing, not_reading_or_writing };

int32_t checkHandshake(int32_t bytesTransfered, bool wasReading,
                       PRFileDesc* ssl_layer_fd, NSSSocketControl* socketInfo);

NSSSocketControl* getSocketInfoIfRunning(PRFileDesc* fd, Operation op) {
  if (!fd || !fd->lower || !fd->secret ||
      fd->identity != nsSSLIOLayerHelpers::nsSSLIOLayerIdentity) {
    NS_ERROR("bad file descriptor passed to getSocketInfoIfRunning");
    PR_SetError(PR_BAD_DESCRIPTOR_ERROR, 0);
    return nullptr;
  }

  NSSSocketControl* socketInfo = (NSSSocketControl*)fd->secret;

  if (socketInfo->IsCanceled()) {
    PRErrorCode err = socketInfo->GetErrorCode();
    PR_SetError(err, 0);
    if (op == reading || op == writing) {
      // We must do TLS intolerance checks for reads and writes, for timeouts
      // in particular.
      (void)checkHandshake(-1, op == reading, fd, socketInfo);
    }

    // If we get here, it is probably because cert verification failed and this
    // is the first I/O attempt since that failure.
    return nullptr;
  }

  return socketInfo;
}

}  // namespace

static PRStatus nsSSLIOLayerConnect(PRFileDesc* fd, const PRNetAddr* addr,
                                    PRIntervalTime timeout) {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p] connecting SSL socket\n", (void*)fd));
  if (!getSocketInfoIfRunning(fd, not_reading_or_writing)) return PR_FAILURE;

  PRStatus status = fd->lower->methods->connect(fd->lower, addr, timeout);
  if (status != PR_SUCCESS) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Error,
            ("[%p] Lower layer connect error: %d\n", (void*)fd, PR_GetError()));
    return status;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("[%p] Connect\n", (void*)fd));
  return status;
}

void nsSSLIOLayerHelpers::rememberTolerantAtVersion(const nsACString& hostName,
                                                    uint16_t port,
                                                    uint16_t tolerant) {
  nsCString key;
  getSiteKey(hostName, port, key);

  MutexAutoLock lock(mutex);

  IntoleranceEntry entry;
  if (mTLSIntoleranceInfo.Get(key, &entry)) {
    entry.AssertInvariant();
    entry.tolerant = std::max(entry.tolerant, tolerant);
    if (entry.intolerant != 0 && entry.intolerant <= entry.tolerant) {
      entry.intolerant = entry.tolerant + 1;
      entry.intoleranceReason = 0;  // lose the reason
    }
  } else {
    entry.tolerant = tolerant;
    entry.intolerant = 0;
    entry.intoleranceReason = 0;
  }

  entry.AssertInvariant();

  mTLSIntoleranceInfo.InsertOrUpdate(key, entry);
}

void nsSSLIOLayerHelpers::forgetIntolerance(const nsACString& hostName,
                                            uint16_t port) {
  nsCString key;
  getSiteKey(hostName, port, key);

  MutexAutoLock lock(mutex);

  IntoleranceEntry entry;
  if (mTLSIntoleranceInfo.Get(key, &entry)) {
    entry.AssertInvariant();

    entry.intolerant = 0;
    entry.intoleranceReason = 0;

    entry.AssertInvariant();
    mTLSIntoleranceInfo.InsertOrUpdate(key, entry);
  }
}

bool nsSSLIOLayerHelpers::fallbackLimitReached(const nsACString& hostName,
                                               uint16_t intolerant) {
  if (isInsecureFallbackSite(hostName)) {
    return intolerant <= SSL_LIBRARY_VERSION_TLS_1_0;
  }
  return intolerant <= mVersionFallbackLimit;
}

// returns true if we should retry the handshake
bool nsSSLIOLayerHelpers::rememberIntolerantAtVersion(
    const nsACString& hostName, uint16_t port, uint16_t minVersion,
    uint16_t intolerant, PRErrorCode intoleranceReason) {
  if (intolerant <= minVersion || fallbackLimitReached(hostName, intolerant)) {
    // We can't fall back any further. Assume that intolerance isn't the issue.
    forgetIntolerance(hostName, port);
    return false;
  }

  nsCString key;
  getSiteKey(hostName, port, key);

  MutexAutoLock lock(mutex);

  IntoleranceEntry entry;
  if (mTLSIntoleranceInfo.Get(key, &entry)) {
    entry.AssertInvariant();
    if (intolerant <= entry.tolerant) {
      // We already know the server is tolerant at an equal or higher version.
      return false;
    }
    if ((entry.intolerant != 0 && intolerant >= entry.intolerant)) {
      // We already know that the server is intolerant at a lower version.
      return true;
    }
  } else {
    entry.tolerant = 0;
  }

  entry.intolerant = intolerant;
  entry.intoleranceReason = intoleranceReason;
  entry.AssertInvariant();
  mTLSIntoleranceInfo.InsertOrUpdate(key, entry);

  return true;
}

void nsSSLIOLayerHelpers::adjustForTLSIntolerance(
    const nsACString& hostName, uint16_t port,
    /*in/out*/ SSLVersionRange& range) {
  IntoleranceEntry entry;

  {
    nsCString key;
    getSiteKey(hostName, port, key);

    MutexAutoLock lock(mutex);
    if (!mTLSIntoleranceInfo.Get(key, &entry)) {
      return;
    }
  }

  entry.AssertInvariant();

  if (entry.intolerant != 0) {
    // We've tried connecting at a higher range but failed, so try at the
    // version we haven't tried yet, unless we have reached the minimum.
    if (range.min < entry.intolerant) {
      range.max = entry.intolerant - 1;
    }
  }
}

PRErrorCode nsSSLIOLayerHelpers::getIntoleranceReason(
    const nsACString& hostName, uint16_t port) {
  IntoleranceEntry entry;

  {
    nsCString key;
    getSiteKey(hostName, port, key);

    MutexAutoLock lock(mutex);
    if (!mTLSIntoleranceInfo.Get(key, &entry)) {
      return 0;
    }
  }

  entry.AssertInvariant();
  return entry.intoleranceReason;
}

bool nsSSLIOLayerHelpers::nsSSLIOLayerInitialized = false;
PRDescIdentity nsSSLIOLayerHelpers::nsSSLIOLayerIdentity;
PRDescIdentity nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity;
PRIOMethods nsSSLIOLayerHelpers::nsSSLIOLayerMethods;
PRIOMethods nsSSLIOLayerHelpers::nsSSLPlaintextLayerMethods;

static PRStatus nsSSLIOLayerClose(PRFileDesc* fd) {
  if (!fd) {
    return PR_FAILURE;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("[%p] Shutting down socket", fd));

  // Take the owning reference from the layer. See the corresponding comment in
  // nsSSLIOLayerAddToSocket where this gets set.
  RefPtr<NSSSocketControl> socketInfo(
      already_AddRefed((NSSSocketControl*)fd->secret));
  fd->secret = nullptr;
  if (!socketInfo) {
    return PR_FAILURE;
  }

  return socketInfo->CloseSocketAndDestroy();
}

#if defined(DEBUG_SSL_VERBOSE) && defined(DUMP_BUFFER)
// Dumps a (potentially binary) buffer using SSM_DEBUG.  (We could have used
// the version in ssltrace.c, but that's specifically tailored to SSLTRACE.)
#  define DUMPBUF_LINESIZE 24
static void nsDumpBuffer(unsigned char* buf, int len) {
  char hexbuf[DUMPBUF_LINESIZE * 3 + 1];
  char chrbuf[DUMPBUF_LINESIZE + 1];
  static const char* hex = "0123456789abcdef";
  int i = 0;
  int l = 0;
  char ch;
  char* c;
  char* h;
  if (len == 0) return;
  hexbuf[DUMPBUF_LINESIZE * 3] = '\0';
  chrbuf[DUMPBUF_LINESIZE] = '\0';
  (void)memset(hexbuf, 0x20, DUMPBUF_LINESIZE * 3);
  (void)memset(chrbuf, 0x20, DUMPBUF_LINESIZE);
  h = hexbuf;
  c = chrbuf;

  while (i < len) {
    ch = buf[i];

    if (l == DUMPBUF_LINESIZE) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("%s%s\n", hexbuf, chrbuf));
      (void)memset(hexbuf, 0x20, DUMPBUF_LINESIZE * 3);
      (void)memset(chrbuf, 0x20, DUMPBUF_LINESIZE);
      h = hexbuf;
      c = chrbuf;
      l = 0;
    }

    // Convert a character to hex.
    *h++ = hex[(ch >> 4) & 0xf];
    *h++ = hex[ch & 0xf];
    h++;

    // Put the character (if it's printable) into the character buffer.
    if ((ch >= 0x20) && (ch <= 0x7e)) {
      *c++ = ch;
    } else {
      *c++ = '.';
    }
    i++;
    l++;
  }
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("%s%s\n", hexbuf, chrbuf));
}

#  define DEBUG_DUMP_BUFFER(buf, len) nsDumpBuffer(buf, len)
#else
#  define DEBUG_DUMP_BUFFER(buf, len)
#endif

namespace {

uint32_t tlsIntoleranceTelemetryBucket(PRErrorCode err) {
  // returns a numeric code for where we track various errors in telemetry
  // only errors that cause version fallback are tracked,
  // so this is also used to determine which errors can cause version fallback
  switch (err) {
    case SSL_ERROR_BAD_MAC_ALERT:
      return 1;
    case SSL_ERROR_BAD_MAC_READ:
      return 2;
    case SSL_ERROR_HANDSHAKE_FAILURE_ALERT:
      return 3;
    case SSL_ERROR_HANDSHAKE_UNEXPECTED_ALERT:
      return 4;
    case SSL_ERROR_ILLEGAL_PARAMETER_ALERT:
      return 6;
    case SSL_ERROR_NO_CYPHER_OVERLAP:
      return 7;
    case SSL_ERROR_UNSUPPORTED_VERSION:
      return 10;
    case SSL_ERROR_PROTOCOL_VERSION_ALERT:
      return 11;
    case SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE:
      return 13;
    case SSL_ERROR_DECODE_ERROR_ALERT:
      return 14;
    case PR_CONNECT_RESET_ERROR:
      return 16;
    case PR_END_OF_FILE_ERROR:
      return 17;
    case SSL_ERROR_INTERNAL_ERROR_ALERT:
      return 18;
    default:
      return 0;
  }
}

bool retryDueToTLSIntolerance(PRErrorCode err, NSSSocketControl* socketInfo) {
  // This function is supposed to decide which error codes should
  // be used to conclude server is TLS intolerant.
  // Note this only happens during the initial SSL handshake.

  if (StaticPrefs::security_tls_ech_disable_grease_on_fallback() &&
      socketInfo->GetEchExtensionStatus() == EchExtensionStatus::kGREASE) {
    // Don't record any intolerances if we used ECH GREASE but force a retry.
    return true;
  }

  if (!socketInfo->IsPreliminaryHandshakeDone() &&
      !socketInfo->HasTls13HandshakeSecrets() && socketInfo->SentMlkemShare()) {
    nsAutoCString errorName;
    const char* prErrorName = PR_ErrorToName(err);
    if (prErrorName) {
      errorName.AppendASCII(prErrorName);
    }
    mozilla::glean::tls::xyber_intolerance_reason.Get(errorName).Add(1);
    // Don't record version intolerance if we sent mlkem768x25519, just force a
    // retry.
    return true;
  }

  SSLVersionRange range = socketInfo->GetTLSVersionRange();

  if (err == SSL_ERROR_UNSUPPORTED_VERSION &&
      range.min == SSL_LIBRARY_VERSION_TLS_1_0) {
    socketInfo->SetSecurityState(nsIWebProgressListener::STATE_IS_INSECURE |
                                 nsIWebProgressListener::STATE_USES_SSL_3);
  }

  // NSS will return SSL_ERROR_RX_MALFORMED_SERVER_HELLO if anti-downgrade
  // detected the downgrade.
  if (err == SSL_ERROR_INAPPROPRIATE_FALLBACK_ALERT ||
      err == SSL_ERROR_RX_MALFORMED_SERVER_HELLO) {
    // This is a clear signal that we've fallen back too many versions.  Treat
    // this as a hard failure, but forget any intolerance so that later attempts
    // don't use this version (i.e., range.max) and trigger the error again.

    // First, track the original cause of the version fallback.  This uses the
    // same buckets as the telemetry below, except that bucket 0 will include
    // all cases where there wasn't an original reason.
    PRErrorCode originalReason = socketInfo->GetTLSIntoleranceReason();
    glean::ssl::version_fallback_inappropriate.AccumulateSingleSample(
        tlsIntoleranceTelemetryBucket(originalReason));

    socketInfo->ForgetTLSIntolerance();

    return false;
  }

  // When not using a proxy we'll see a connection reset error.
  // When using a proxy, we'll see an end of file error.

  // Don't allow STARTTLS connections to fall back on connection resets or
  // EOF.
  if ((err == PR_CONNECT_RESET_ERROR || err == PR_END_OF_FILE_ERROR) &&
      socketInfo->GetForSTARTTLS()) {
    return false;
  }

  uint32_t reason = tlsIntoleranceTelemetryBucket(err);
  if (reason == 0) {
    return false;
  }

  // The difference between _PRE and _POST represents how often we avoided
  // TLS intolerance fallback due to remembered tolerance.

  switch (range.max) {
    case SSL_LIBRARY_VERSION_TLS_1_3:
      glean::ssl::tls13_intolerance_reason_pre.AccumulateSingleSample(reason);
      break;
    case SSL_LIBRARY_VERSION_TLS_1_2:
      glean::ssl::tls12_intolerance_reason_pre.AccumulateSingleSample(reason);
      break;
    case SSL_LIBRARY_VERSION_TLS_1_1:
      glean::ssl::tls11_intolerance_reason_pre.AccumulateSingleSample(reason);
      break;
    case SSL_LIBRARY_VERSION_TLS_1_0:
      glean::ssl::tls10_intolerance_reason_pre.AccumulateSingleSample(reason);
      break;
    default:
      MOZ_CRASH("impossible TLS version");
      return false;
  }

  if (!socketInfo->RememberTLSIntolerant(err)) {
    return false;
  }

  switch (range.max) {
    case SSL_LIBRARY_VERSION_TLS_1_3:
      glean::ssl::tls13_intolerance_reason_post.AccumulateSingleSample(reason);
      break;
    case SSL_LIBRARY_VERSION_TLS_1_2:
      glean::ssl::tls12_intolerance_reason_post.AccumulateSingleSample(reason);
      break;
    case SSL_LIBRARY_VERSION_TLS_1_1:
      glean::ssl::tls11_intolerance_reason_post.AccumulateSingleSample(reason);
      break;
    case SSL_LIBRARY_VERSION_TLS_1_0:
      glean::ssl::tls10_intolerance_reason_post.AccumulateSingleSample(reason);
      break;
    default:
      MOZ_CRASH("impossible TLS version");
      return false;
  }

  return true;
}

// Ensure that we haven't added too many errors to fit.
static_assert((SSL_ERROR_END_OF_LIST - SSL_ERROR_BASE) <= 256,
              "too many SSL errors");
static_assert((SEC_ERROR_END_OF_LIST - SEC_ERROR_BASE) <= 256,
              "too many SEC errors");
static_assert((PR_MAX_ERROR - PR_NSPR_ERROR_BASE) <= 128,
              "too many NSPR errors");
static_assert((mozilla::pkix::ERROR_BASE - mozilla::pkix::END_OF_LIST) < 31,
              "too many moz::pkix errors");

static void reportHandshakeResult(int32_t bytesTransferred, bool wasReading,
                                  PRErrorCode err,
                                  NSSSocketControl* socketInfo) {
  uint32_t bucket;

  // A negative bytesTransferred or a 0 read are errors.
  if (bytesTransferred > 0) {
    bucket = 0;
  } else if ((bytesTransferred == 0) && !wasReading) {
    // PR_Write() is defined to never return 0, but let's make sure.
    // https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSPR/Reference/PR_Write.
    MOZ_ASSERT(false);
    bucket = 671;
  } else if (IS_SSL_ERROR(err)) {
    bucket = err - SSL_ERROR_BASE;
    MOZ_ASSERT(bucket > 0);  // SSL_ERROR_EXPORT_ONLY_SERVER isn't used.
  } else if (IS_SEC_ERROR(err)) {
    bucket = (err - SEC_ERROR_BASE) + 256;
  } else if ((err >= PR_NSPR_ERROR_BASE) && (err < PR_MAX_ERROR)) {
    bucket = (err - PR_NSPR_ERROR_BASE) + 512;
  } else if ((err >= mozilla::pkix::ERROR_BASE) &&
             (err < mozilla::pkix::ERROR_LIMIT)) {
    bucket = (err - mozilla::pkix::ERROR_BASE) + 640;
  } else {
    bucket = 671;
  }

  uint32_t flags = socketInfo->GetProviderFlags();
  if (!(flags & nsISocketProvider::IS_RETRY)) {
    glean::ssl_handshake::result_first_try.AccumulateSingleSample(bucket);
  }

  if (flags & nsISocketProvider::BE_CONSERVATIVE) {
    glean::ssl_handshake::result_conservative.AccumulateSingleSample(bucket);
  }

  switch (socketInfo->GetEchExtensionStatus()) {
    case EchExtensionStatus::kGREASE:
      glean::ssl_handshake::result_ech_grease.AccumulateSingleSample(bucket);
      break;
    case EchExtensionStatus::kReal:
      glean::ssl_handshake::result_ech.AccumulateSingleSample(bucket);
      break;
    default:
      break;
  }
  glean::ssl_handshake::result.AccumulateSingleSample(bucket);

  if (bucket == 0) {
    nsCOMPtr<nsITransportSecurityInfo> securityInfo;
    if (NS_FAILED(socketInfo->GetSecurityInfo(getter_AddRefs(securityInfo))) ||
        !securityInfo) {
      return;
    }
    // Web Privacy Telemetry for successful connections.
    bool success = true;

    bool usedPrivateDNS = false;
    success &= securityInfo->GetUsedPrivateDNS(&usedPrivateDNS) == NS_OK;

    bool madeOCSPRequest = false;
    success &= securityInfo->GetMadeOCSPRequests(&madeOCSPRequest) == NS_OK;

    uint16_t protocolVersion = 0;
    success &= securityInfo->GetProtocolVersion(&protocolVersion) == NS_OK;
    bool usedTLS13 = protocolVersion == 4;

    bool usedECH = false;
    success &= securityInfo->GetIsAcceptedEch(&usedECH) == NS_OK;

    // As bucket is 0 we are reporting the results of a sucessful connection
    // and so TransportSecurityInfo should be populated. However, this isn't
    // happening in all cases, see Bug 1789458.
    if (success) {
      uint8_t TLSPrivacyResult = 0;
      TLSPrivacyResult |= usedTLS13 << 0;
      TLSPrivacyResult |= !madeOCSPRequest << 1;
      TLSPrivacyResult |= usedPrivateDNS << 2;
      TLSPrivacyResult |= usedECH << 3;

      glean::ssl_handshake::privacy.AccumulateSingleSample(TLSPrivacyResult);
    }
  }
}

// Check the status of the handshake. This is where PSM checks for TLS
// intolerance and potentially sets up TLS intolerance fallback by noting the
// intolerance, setting the NSPR error to PR_CONNECT_RESET_ERROR, and returning
// -1 as the bytes transferred so that necko retries the connection.
// Otherwise, PSM returns the bytes transferred unchanged.
int32_t checkHandshake(int32_t bytesTransferred, bool wasReading,
                       PRFileDesc* ssl_layer_fd, NSSSocketControl* socketInfo) {
  const PRErrorCode originalError = PR_GetError();

  // If the connection would block, return early.
  if (bytesTransferred < 0 && originalError == PR_WOULD_BLOCK_ERROR) {
    PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
    return bytesTransferred;
  }

  // We only need to do TLS intolerance checking for the first transfer.
  bool handleHandshakeResultNow = socketInfo->IsHandshakePending();
  if (!handleHandshakeResultNow) {
    // If we've encountered an error since the handshake, ensure the socket
    // control is cancelled, so that getSocketInfoIfRunning will correctly
    // cause us to fail if another part of Gecko (erroneously) calls an I/O
    // function (PR_Send/PR_Recv/etc.) again on this socket.
    if (bytesTransferred < 0) {
      if (!socketInfo->IsCanceled()) {
        socketInfo->SetCanceled(originalError);
      }
      PR_SetError(originalError, 0);
    }
    return bytesTransferred;
  }

  // TLS intolerant servers only cause the first transfer to fail, so let's
  // set the HandshakePending attribute to false so that we don't try this logic
  // again in a subsequent transfer.
  socketInfo->SetHandshakeNotPending();
  // Report the result once for each handshake. Note that this does not
  // get handshakes which are cancelled before any reads or writes
  // happen.
  reportHandshakeResult(bytesTransferred, wasReading, originalError,
                        socketInfo);

  // If there was no error, return early. The case where we read 0 bytes is not
  // considered an error by NSS, but PSM interprets this as TLS intolerance, so
  // we turn it into an error. Writes of 0 bytes are an error, because PR_Write
  // is never supposed to return 0.
  if (bytesTransferred > 0) {
    return bytesTransferred;
  }

  // There was some sort of error. Determine what it was and if we want to
  // retry the connection due to TLS intolerance.
  PRErrorCode errorToUse = originalError;
  // Turn zero-length reads into errors and handle zero-length write errors.
  if (bytesTransferred == 0) {
    if (wasReading) {
      errorToUse = PR_END_OF_FILE_ERROR;
    } else {
      errorToUse = SEC_ERROR_LIBRARY_FAILURE;
    }
    bytesTransferred = -1;
  }
  bool wantRetry = retryDueToTLSIntolerance(errorToUse, socketInfo);
  // Set the error on the socket control and cancel it.
  if (!socketInfo->IsCanceled()) {
    socketInfo->SetCanceled(errorToUse);
  }

  if (wantRetry) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[%p] checkHandshake: will retry with lower max TLS version",
             ssl_layer_fd));
    // Setting the error PR_CONNECT_RESET_ERROR causes necko to retry the
    // connection.
    PR_SetError(PR_CONNECT_RESET_ERROR, 0);
  } else {
    PR_SetError(originalError, 0);
  }

  return bytesTransferred;
}

}  // namespace

static int16_t nsSSLIOLayerPoll(PRFileDesc* fd, int16_t in_flags,
                                int16_t* out_flags) {
  if (!out_flags) {
    NS_WARNING("nsSSLIOLayerPoll called with null out_flags");
    return 0;
  }

  *out_flags = 0;

  NSSSocketControl* socketInfo =
      getSocketInfoIfRunning(fd, not_reading_or_writing);

  if (!socketInfo) {
    // If we get here, it is probably because certificate validation failed
    // and this is the first I/O operation after the failure.
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("[%p] polling SSL socket right after certificate verification failed "
         "or NSS shutdown or SDR logout %d\n",
         fd, (int)in_flags));

    MOZ_ASSERT(in_flags & PR_POLL_EXCEPT,
               "Caller did not poll for EXCEPT (canceled)");
    // Since this poll method cannot return errors, we want the caller to call
    // PR_Send/PR_Recv right away to get the error, so we tell that we are
    // ready for whatever I/O they are asking for. (See getSocketInfoIfRunning).
    *out_flags = in_flags | PR_POLL_EXCEPT;  // see also bug 480619
    return in_flags;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Verbose,
          (socketInfo->IsWaitingForCertVerification()
               ? "[%p] polling SSL socket during certificate verification "
                 "using lower %d\n"
               : "[%p] poll SSL socket using lower %d\n",
           fd, (int)in_flags));

  socketInfo->MaybeDispatchSelectClientAuthCertificate();

  // We want the handshake to continue during certificate validation, so we
  // don't need to do anything special here. libssl automatically blocks when
  // it reaches any point that would be unsafe to send/receive something before
  // cert validation is complete.
  int16_t result = fd->lower->methods->poll(fd->lower, in_flags, out_flags);
  MOZ_LOG(gPIPNSSLog, LogLevel::Verbose,
          ("[%p] poll SSL socket returned %d\n", (void*)fd, (int)result));
  return result;
}

nsSSLIOLayerHelpers::nsSSLIOLayerHelpers(PublicOrPrivate aPublicOrPrivate,
                                         uint32_t aTlsFlags)
    : mVersionFallbackLimit(SSL_LIBRARY_VERSION_TLS_1_0),
      mPublicOrPrivate(aPublicOrPrivate),
      mutex("nsSSLIOLayerHelpers.mutex"),
      mTlsFlags(aTlsFlags) {}

// PSMAvailable and PSMAvailable64 are reachable, but they're unimplemented in
// PSM, so we set an error and return -1.
static int32_t PSMAvailable(PRFileDesc*) {
  PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
  return -1;
}

static int64_t PSMAvailable64(PRFileDesc*) {
  PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
  return -1;
}

static PRStatus PSMGetsockname(PRFileDesc* fd, PRNetAddr* addr) {
  if (!getSocketInfoIfRunning(fd, not_reading_or_writing)) return PR_FAILURE;

  return fd->lower->methods->getsockname(fd->lower, addr);
}

static PRStatus PSMGetpeername(PRFileDesc* fd, PRNetAddr* addr) {
  if (!getSocketInfoIfRunning(fd, not_reading_or_writing)) return PR_FAILURE;

  return fd->lower->methods->getpeername(fd->lower, addr);
}

static PRStatus PSMGetsocketoption(PRFileDesc* fd, PRSocketOptionData* data) {
  if (!getSocketInfoIfRunning(fd, not_reading_or_writing)) return PR_FAILURE;

  return fd->lower->methods->getsocketoption(fd, data);
}

static PRStatus PSMSetsocketoption(PRFileDesc* fd,
                                   const PRSocketOptionData* data) {
  if (!getSocketInfoIfRunning(fd, not_reading_or_writing)) return PR_FAILURE;

  return fd->lower->methods->setsocketoption(fd, data);
}

static int32_t PSMRecv(PRFileDesc* fd, void* buf, int32_t amount, int flags,
                       PRIntervalTime timeout) {
  NSSSocketControl* socketInfo = getSocketInfoIfRunning(fd, reading);
  if (!socketInfo) return -1;

  if (flags != PR_MSG_PEEK && flags != 0) {
    PR_SetError(PR_INVALID_ARGUMENT_ERROR, 0);
    return -1;
  }

  int32_t bytesRead =
      fd->lower->methods->recv(fd->lower, buf, amount, flags, timeout);

  MOZ_LOG(gPIPNSSLog, LogLevel::Verbose,
          ("[%p] read %d bytes\n", (void*)fd, bytesRead));

#ifdef DEBUG_SSL_VERBOSE
  DEBUG_DUMP_BUFFER((unsigned char*)buf, bytesRead);
#endif

  return checkHandshake(bytesRead, true, fd, socketInfo);
}

static int32_t PSMSend(PRFileDesc* fd, const void* buf, int32_t amount,
                       int flags, PRIntervalTime timeout) {
  NSSSocketControl* socketInfo = getSocketInfoIfRunning(fd, writing);
  if (!socketInfo) return -1;

  if (flags != 0) {
    PR_SetError(PR_INVALID_ARGUMENT_ERROR, 0);
    return -1;
  }

#ifdef DEBUG_SSL_VERBOSE
  DEBUG_DUMP_BUFFER((unsigned char*)buf, amount);
#endif

  if (socketInfo->IsShortWritePending() && amount > 0) {
    // We got "SSL short write" last time, try to flush the pending byte.
#ifdef DEBUG
    socketInfo->CheckShortWrittenBuffer(static_cast<const unsigned char*>(buf),
                                        amount);
#endif

    buf = socketInfo->GetShortWritePendingByteRef();
    amount = 1;

    MOZ_LOG(gPIPNSSLog, LogLevel::Verbose,
            ("[%p] pushing 1 byte after SSL short write", fd));
  }

  int32_t bytesWritten =
      fd->lower->methods->send(fd->lower, buf, amount, flags, timeout);

  // NSS indicates that it can't write all requested data (due to network
  // congestion, for example) by returning either one less than the amount
  // of data requested or 16383, if the requested amount is greater than
  // 16384. We refer to this as a "short write". If we simply returned
  // the amount that NSS did write, the layer above us would then call
  // PSMSend with a very small amount of data (often 1). This is inefficient
  // and can lead to alternating between sending large packets and very small
  // packets. To prevent this, we alert the layer calling us that the operation
  // would block and that it should be retried later, with the same data.
  // When it does, we tell NSS to write the remaining byte it didn't write
  // in the previous call. We then return the total number of bytes written,
  // which is the number that caused the short write plus the additional byte
  // we just wrote out.

  // The 16384 value is based on libssl's maximum buffer size:
  //    MAX_FRAGMENT_LENGTH - 1
  //
  // It's in a private header, though, filed bug 1394822 to expose it.
  static const int32_t kShortWrite16k = 16383;

  if ((amount > 1 && bytesWritten == (amount - 1)) ||
      (amount > kShortWrite16k && bytesWritten == kShortWrite16k)) {
    // This is indication of an "SSL short write", block to force retry.
    socketInfo->SetShortWritePending(
        bytesWritten + 1,  // The amount to return after the flush
        *(static_cast<const unsigned char*>(buf) + bytesWritten));

    MOZ_LOG(
        gPIPNSSLog, LogLevel::Verbose,
        ("[%p] indicated SSL short write for %d bytes (written just %d bytes)",
         fd, amount, bytesWritten));

    bytesWritten = -1;
    PR_SetError(PR_WOULD_BLOCK_ERROR, 0);

#ifdef DEBUG
    socketInfo->RememberShortWrittenBuffer(
        static_cast<const unsigned char*>(buf));
#endif

  } else if (socketInfo->IsShortWritePending() && bytesWritten == 1) {
    // We have now flushed all pending data in the SSL socket
    // after the indicated short write.  Tell the upper layer
    // it has sent all its data now.
    MOZ_LOG(gPIPNSSLog, LogLevel::Verbose,
            ("[%p] finished SSL short write", fd));

    bytesWritten = socketInfo->ResetShortWritePending();
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Verbose,
          ("[%p] wrote %d bytes\n", fd, bytesWritten));

  return checkHandshake(bytesWritten, false, fd, socketInfo);
}

static PRStatus PSMBind(PRFileDesc* fd, const PRNetAddr* addr) {
  if (!getSocketInfoIfRunning(fd, not_reading_or_writing)) return PR_FAILURE;

  return fd->lower->methods->bind(fd->lower, addr);
}

static int32_t nsSSLIOLayerRead(PRFileDesc* fd, void* buf, int32_t amount) {
  return PSMRecv(fd, buf, amount, 0, PR_INTERVAL_NO_TIMEOUT);
}

static int32_t nsSSLIOLayerWrite(PRFileDesc* fd, const void* buf,
                                 int32_t amount) {
  return PSMSend(fd, buf, amount, 0, PR_INTERVAL_NO_TIMEOUT);
}

static PRStatus PSMConnectcontinue(PRFileDesc* fd, int16_t out_flags) {
  if (!getSocketInfoIfRunning(fd, not_reading_or_writing)) {
    return PR_FAILURE;
  }

  return fd->lower->methods->connectcontinue(fd, out_flags);
}

NS_IMPL_ISUPPORTS(nsSSLIOLayerHelpers, nsIObserver)

NS_IMETHODIMP
nsSSLIOLayerHelpers::Observe(nsISupports* aSubject, const char* aTopic,
                             const char16_t* someData) {
  if (nsCRT::strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID) == 0) {
    NS_ConvertUTF16toUTF8 prefName(someData);

    if (prefName.EqualsLiteral("security.tls.version.fallback-limit")) {
      loadVersionFallbackLimit();
    } else if (prefName.EqualsLiteral("security.tls.insecure_fallback_hosts")) {
      initInsecureFallbackSites();
    }
  } else if (nsCRT::strcmp(aTopic, "last-pb-context-exited") == 0) {
    clearStoredData();
  }
  return NS_OK;
}

void nsSSLIOLayerHelpers::GlobalInit() {
  MOZ_ASSERT(NS_IsMainThread(), "Not on main thread");
  gPublicSSLIOLayerHelpers = new nsSSLIOLayerHelpers(PublicOrPrivate::Public);
  gPublicSSLIOLayerHelpers->Init();
  gPrivateSSLIOLayerHelpers = new nsSSLIOLayerHelpers(PublicOrPrivate::Private);
  gPrivateSSLIOLayerHelpers->Init();
}

/*static*/
void nsSSLIOLayerHelpers::GlobalCleanup() {
  MOZ_ASSERT(NS_IsMainThread(), "Not on main thread");

  if (gPrivateSSLIOLayerHelpers) {
    gPrivateSSLIOLayerHelpers = nullptr;
  }

  if (gPublicSSLIOLayerHelpers) {
    gPublicSSLIOLayerHelpers = nullptr;
  }
}

already_AddRefed<nsSSLIOLayerHelpers> PublicSSLIOLayerHelpers() {
  return do_AddRef(gPublicSSLIOLayerHelpers);
}

already_AddRefed<nsSSLIOLayerHelpers> PrivateSSLIOLayerHelpers() {
  return do_AddRef(gPrivateSSLIOLayerHelpers);
}

static int32_t PlaintextRecv(PRFileDesc* fd, void* buf, int32_t amount,
                             int flags, PRIntervalTime timeout) {
  NSSSocketControl* socketInfo = nullptr;

  int32_t bytesRead =
      fd->lower->methods->recv(fd->lower, buf, amount, flags, timeout);
  if (fd->identity == nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity) {
    socketInfo = (NSSSocketControl*)fd->secret;
  }

  if ((bytesRead > 0) && socketInfo) {
    socketInfo->AddPlaintextBytesRead(bytesRead);
  }
  return bytesRead;
}

nsSSLIOLayerHelpers::~nsSSLIOLayerHelpers() {
  Preferences::RemoveObserver(this, "security.tls.version.fallback-limit");
  Preferences::RemoveObserver(this, "security.tls.insecure_fallback_hosts");
}

template <typename R, R return_value, typename... Args>
static R InvalidPRIOMethod(Args...) {
  MOZ_ASSERT_UNREACHABLE("I/O method is invalid");
  PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
  return return_value;
}

nsresult nsSSLIOLayerHelpers::Init() {
  if (!nsSSLIOLayerInitialized) {
    MOZ_ASSERT(NS_IsMainThread());
    nsSSLIOLayerInitialized = true;
    nsSSLIOLayerIdentity = PR_GetUniqueIdentity("NSS layer");
    nsSSLIOLayerMethods = *PR_GetDefaultIOMethods();

    nsSSLIOLayerMethods.fsync =
        InvalidPRIOMethod<PRStatus, PR_FAILURE, PRFileDesc*>;
    nsSSLIOLayerMethods.seek =
        InvalidPRIOMethod<int32_t, -1, PRFileDesc*, int32_t, PRSeekWhence>;
    nsSSLIOLayerMethods.seek64 =
        InvalidPRIOMethod<int64_t, -1, PRFileDesc*, int64_t, PRSeekWhence>;
    nsSSLIOLayerMethods.fileInfo =
        InvalidPRIOMethod<PRStatus, PR_FAILURE, PRFileDesc*, PRFileInfo*>;
    nsSSLIOLayerMethods.fileInfo64 =
        InvalidPRIOMethod<PRStatus, PR_FAILURE, PRFileDesc*, PRFileInfo64*>;
    nsSSLIOLayerMethods.writev =
        InvalidPRIOMethod<int32_t, -1, PRFileDesc*, const PRIOVec*, int32_t,
                          PRIntervalTime>;
    nsSSLIOLayerMethods.accept =
        InvalidPRIOMethod<PRFileDesc*, nullptr, PRFileDesc*, PRNetAddr*,
                          PRIntervalTime>;
    nsSSLIOLayerMethods.listen =
        InvalidPRIOMethod<PRStatus, PR_FAILURE, PRFileDesc*, int>;
    nsSSLIOLayerMethods.shutdown =
        InvalidPRIOMethod<PRStatus, PR_FAILURE, PRFileDesc*, int>;
    nsSSLIOLayerMethods.recvfrom =
        InvalidPRIOMethod<int32_t, -1, PRFileDesc*, void*, int32_t, int,
                          PRNetAddr*, PRIntervalTime>;
    nsSSLIOLayerMethods.sendto =
        InvalidPRIOMethod<int32_t, -1, PRFileDesc*, const void*, int32_t, int,
                          const PRNetAddr*, PRIntervalTime>;
    nsSSLIOLayerMethods.acceptread =
        InvalidPRIOMethod<int32_t, -1, PRFileDesc*, PRFileDesc**, PRNetAddr**,
                          void*, int32_t, PRIntervalTime>;
    nsSSLIOLayerMethods.transmitfile =
        InvalidPRIOMethod<int32_t, -1, PRFileDesc*, PRFileDesc*, const void*,
                          int32_t, PRTransmitFileFlags, PRIntervalTime>;
    nsSSLIOLayerMethods.sendfile =
        InvalidPRIOMethod<int32_t, -1, PRFileDesc*, PRSendFileData*,
                          PRTransmitFileFlags, PRIntervalTime>;

    nsSSLIOLayerMethods.available = PSMAvailable;
    nsSSLIOLayerMethods.available64 = PSMAvailable64;
    nsSSLIOLayerMethods.getsockname = PSMGetsockname;
    nsSSLIOLayerMethods.getpeername = PSMGetpeername;
    nsSSLIOLayerMethods.getsocketoption = PSMGetsocketoption;
    nsSSLIOLayerMethods.setsocketoption = PSMSetsocketoption;
    nsSSLIOLayerMethods.recv = PSMRecv;
    nsSSLIOLayerMethods.send = PSMSend;
    nsSSLIOLayerMethods.connectcontinue = PSMConnectcontinue;
    nsSSLIOLayerMethods.bind = PSMBind;

    nsSSLIOLayerMethods.connect = nsSSLIOLayerConnect;
    nsSSLIOLayerMethods.close = nsSSLIOLayerClose;
    nsSSLIOLayerMethods.write = nsSSLIOLayerWrite;
    nsSSLIOLayerMethods.read = nsSSLIOLayerRead;
    nsSSLIOLayerMethods.poll = nsSSLIOLayerPoll;

    nsSSLPlaintextLayerIdentity = PR_GetUniqueIdentity("Plaintxext PSM layer");
    nsSSLPlaintextLayerMethods = *PR_GetDefaultIOMethods();
    nsSSLPlaintextLayerMethods.recv = PlaintextRecv;
  }

  loadVersionFallbackLimit();

  // non main thread helpers will need to use defaults
  if (NS_IsMainThread()) {
    initInsecureFallbackSites();

    Preferences::AddStrongObserver(this, "security.tls.version.fallback-limit");
    if (isPublic()) {
      // Changes to the allowlist on the public side will update the pref.
      // Don't propagate the changes to the private side.
      Preferences::AddStrongObserver(this,
                                     "security.tls.insecure_fallback_hosts");
    } else {
      nsCOMPtr<nsIObserverService> obsSvc =
          mozilla::services::GetObserverService();
      if (obsSvc) {
        obsSvc->AddObserver(this, "last-pb-context-exited", false);
      }
    }
  } else {
    MOZ_ASSERT(mTlsFlags, "Only per socket version can ignore prefs");
  }

  return NS_OK;
}

void nsSSLIOLayerHelpers::loadVersionFallbackLimit() {
  // see nsNSSComponent::SetEnabledTLSVersions for pref handling rules
  uint32_t limit = StaticPrefs::security_tls_version_fallback_limit();

  // set fallback limit if it is set in the tls flags
  uint32_t tlsFlagsFallbackLimit = getTLSProviderFlagFallbackLimit(mTlsFlags);

  if (tlsFlagsFallbackLimit) {
    limit = tlsFlagsFallbackLimit;
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("loadVersionFallbackLimit overriden by tlsFlags %d\n", limit));
  }

  SSLVersionRange defaults = {SSL_LIBRARY_VERSION_TLS_1_2,
                              SSL_LIBRARY_VERSION_TLS_1_2};
  SSLVersionRange filledInRange;
  nsNSSComponent::FillTLSVersionRange(filledInRange, limit, limit, defaults);
  if (filledInRange.max < SSL_LIBRARY_VERSION_TLS_1_2) {
    filledInRange.max = SSL_LIBRARY_VERSION_TLS_1_2;
  }

  mVersionFallbackLimit = filledInRange.max;
}

void nsSSLIOLayerHelpers::clearStoredData() {
  MOZ_ASSERT(NS_IsMainThread());
  initInsecureFallbackSites();

  MutexAutoLock lock(mutex);
  mTLSIntoleranceInfo.Clear();
}

void nsSSLIOLayerHelpers::setInsecureFallbackSites(const nsCString& str) {
  MutexAutoLock lock(mutex);

  mInsecureFallbackSites.Clear();

  for (const nsACString& host : nsCCharSeparatedTokenizer(str, ',').ToRange()) {
    if (!host.IsEmpty()) {
      mInsecureFallbackSites.PutEntry(host);
    }
  }
}

void nsSSLIOLayerHelpers::initInsecureFallbackSites() {
  MOZ_ASSERT(NS_IsMainThread());
  nsAutoCString insecureFallbackHosts;
  Preferences::GetCString("security.tls.insecure_fallback_hosts",
                          insecureFallbackHosts);
  setInsecureFallbackSites(insecureFallbackHosts);
}

bool nsSSLIOLayerHelpers::isPublic() const {
  return mPublicOrPrivate == PublicOrPrivate::Public;
}

class FallbackPrefRemover final : public Runnable {
 public:
  explicit FallbackPrefRemover(const nsACString& aHost)
      : mozilla::Runnable("FallbackPrefRemover"), mHost(aHost) {}
  NS_IMETHOD Run() override;

 private:
  nsCString mHost;
};

NS_IMETHODIMP
FallbackPrefRemover::Run() {
  MOZ_ASSERT(NS_IsMainThread());
  nsAutoCString oldValue;
  Preferences::GetCString("security.tls.insecure_fallback_hosts", oldValue);
  nsCString newValue;
  for (const nsACString& host :
       nsCCharSeparatedTokenizer(oldValue, ',').ToRange()) {
    if (host.Equals(mHost)) {
      continue;
    }
    if (!newValue.IsEmpty()) {
      newValue.Append(',');
    }
    newValue.Append(host);
  }
  Preferences::SetCString("security.tls.insecure_fallback_hosts", newValue);
  return NS_OK;
}

void nsSSLIOLayerHelpers::removeInsecureFallbackSite(const nsACString& hostname,
                                                     uint16_t port) {
  forgetIntolerance(hostname, port);
  {
    MutexAutoLock lock(mutex);
    if (!mInsecureFallbackSites.Contains(hostname)) {
      return;
    }
    mInsecureFallbackSites.RemoveEntry(hostname);
  }
  if (!isPublic()) {
    return;
  }
  RefPtr<Runnable> runnable = new FallbackPrefRemover(hostname);
  if (NS_IsMainThread()) {
    runnable->Run();
  } else {
    NS_DispatchToMainThread(runnable);
  }
}

bool nsSSLIOLayerHelpers::isInsecureFallbackSite(const nsACString& hostname) {
  MutexAutoLock lock(mutex);
  return mInsecureFallbackSites.Contains(hostname);
}

nsresult nsSSLIOLayerNewSocket(int32_t family, const char* host, int32_t port,
                               nsIProxyInfo* proxy,
                               const OriginAttributes& originAttributes,
                               PRFileDesc** fd,
                               nsITLSSocketControl** tlsSocketControl,
                               bool forSTARTTLS, uint32_t flags,
                               uint32_t tlsFlags) {
  PRFileDesc* sock = PR_OpenTCPSocket(family);
  if (!sock) return NS_ERROR_OUT_OF_MEMORY;

  nsresult rv =
      nsSSLIOLayerAddToSocket(family, host, port, proxy, originAttributes, sock,
                              tlsSocketControl, forSTARTTLS, flags, tlsFlags);
  if (NS_FAILED(rv)) {
    PR_Close(sock);
    return rv;
  }

  *fd = sock;
  return NS_OK;
}

static PRFileDesc* nsSSLIOLayerImportFD(PRFileDesc* fd,
                                        NSSSocketControl* infoObject,
                                        const char* host, bool haveHTTPSProxy) {
  // Memory allocated here is released when fd is closed, regardless of the
  // success of this function.
  PRFileDesc* sslSock = SSL_ImportFD(nullptr, fd);
  if (!sslSock) {
    return nullptr;
  }
  if (SSL_SetPKCS11PinArg(sslSock, infoObject) != SECSuccess) {
    return nullptr;
  }
  if (SSL_HandshakeCallback(sslSock, HandshakeCallback, infoObject) !=
      SECSuccess) {
    return nullptr;
  }
  if (SSL_SecretCallback(sslSock, SecretCallback, infoObject) != SECSuccess) {
    return nullptr;
  }
  if (SSL_SetCanFalseStartCallback(sslSock, CanFalseStartCallback,
                                   infoObject) != SECSuccess) {
    return nullptr;
  }

  // Disable this hook if we connect anonymously. See bug 466080.
  uint32_t flags = infoObject->GetProviderFlags();
  SSLGetClientAuthData clientAuthDataHook = SSLGetClientAuthDataHook;
  // Provide the client cert to HTTPS proxy no matter if it is anonymous.
  if (flags & nsISocketProvider::ANONYMOUS_CONNECT && !haveHTTPSProxy &&
      !(flags & nsISocketProvider::ANONYMOUS_CONNECT_ALLOW_CLIENT_CERT)) {
    clientAuthDataHook = nullptr;
  }
  if (SSL_GetClientAuthDataHook(sslSock, clientAuthDataHook, infoObject) !=
      SECSuccess) {
    return nullptr;
  }

  if (SSL_AuthCertificateHook(sslSock, AuthCertificateHook, infoObject) !=
      SECSuccess) {
    return nullptr;
  }
  if (SSL_SetURL(sslSock, host) != SECSuccess) {
    return nullptr;
  }

  return sslSock;
}

// Please change getSignatureName in nsNSSCallbacks.cpp when changing the list
// here. See NOTE at SSL_SignatureSchemePrefSet call site.
static const SSLSignatureScheme sEnabledSignatureSchemes[] = {
    ssl_sig_ecdsa_secp256r1_sha256,
    ssl_sig_ecdsa_secp384r1_sha384,
    ssl_sig_ecdsa_secp521r1_sha512,
    ssl_sig_rsa_pss_sha256,
    ssl_sig_rsa_pss_sha384,
    ssl_sig_rsa_pss_sha512,
    ssl_sig_rsa_pkcs1_sha256,
    ssl_sig_rsa_pkcs1_sha384,
    ssl_sig_rsa_pkcs1_sha512,
#if !defined(EARLY_BETA_OR_EARLIER)
    ssl_sig_ecdsa_sha1,
#endif
    ssl_sig_rsa_pkcs1_sha1,
};

enum CertificateCompressionAlgorithms {
  zlib = 0x01,
  brotli = 0x02,
  zstd = 0x03
};

void GatherCertificateCompressionTelemetry(SECStatus rv,
                                           CertificateCompressionAlgorithms alg,
                                           PRUint64 actualCertLen,
                                           PRUint64 encodedCertLen) {
  nsAutoCString decoder;

  switch (alg) {
    case zlib:
      decoder.AssignLiteral("zlib");
      break;
    case brotli:
      decoder.AssignLiteral("brotli");
      break;
    case zstd:
      decoder.AssignLiteral("zstd");
      break;
  }

  if (rv != SECSuccess) {
    mozilla::glean::cert_compression::failures.Get(decoder).Add(1);
    return;
  }
  // Glam requires us to send 0 in case of success.
  mozilla::glean::cert_compression::failures.Get(decoder).Add(0);
}

SECStatus zlibCertificateDecode(const SECItem* input, unsigned char* output,
                                size_t outputLen, size_t* usedLen) {
  SECStatus rv = SECFailure;
  if (!input || !input->data || input->len == 0 || !output || outputLen == 0) {
    PR_SetError(SEC_ERROR_INVALID_ARGS, 0);
    return rv;
  }

  z_stream strm = {};

  if (inflateInit(&strm) != Z_OK) {
    PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
    return rv;
  }

  auto cleanup = MakeScopeExit([&] {
    GatherCertificateCompressionTelemetry(rv, zlib, *usedLen, input->len);
    (void)inflateEnd(&strm);
  });

  strm.avail_in = input->len;
  strm.next_in = input->data;

  strm.avail_out = outputLen;
  strm.next_out = output;

  int ret = inflate(&strm, Z_FINISH);
  bool ok = ret == Z_STREAM_END && strm.avail_in == 0 && strm.avail_out == 0;
  if (!ok) {
    PR_SetError(SEC_ERROR_BAD_DATA, 0);
    return rv;
  }

  *usedLen = strm.total_out;
  rv = SECSuccess;
  return rv;
}

SECStatus brotliCertificateDecode(const SECItem* input, unsigned char* output,
                                  size_t outputLen, size_t* usedLen) {
  SECStatus rv = SECFailure;

  if (!input || !input->data || input->len == 0 || !output || outputLen == 0) {
    PR_SetError(SEC_ERROR_INVALID_ARGS, 0);
    return rv;
  }

  auto cleanup = MakeScopeExit([&] {
    GatherCertificateCompressionTelemetry(rv, brotli, *usedLen, input->len);
  });

  size_t uncompressedSize = outputLen;
  BrotliDecoderResult result = BrotliDecoderDecompress(
      input->len, input->data, &uncompressedSize, output);

  if (result != BROTLI_DECODER_RESULT_SUCCESS) {
    PR_SetError(SEC_ERROR_BAD_DATA, 0);
    return rv;
  }

  *usedLen = uncompressedSize;
  rv = SECSuccess;
  return rv;
}

SECStatus zstdCertificateDecode(const SECItem* input, unsigned char* output,
                                size_t outputLen, size_t* usedLen) {
  SECStatus rv = SECFailure;

  if (!input || !input->data || input->len == 0 || !output || outputLen == 0) {
    PR_SetError(SEC_ERROR_INVALID_ARGS, 0);
    return rv;
  }

  auto cleanup = MakeScopeExit([&] {
    GatherCertificateCompressionTelemetry(rv, zstd, *usedLen, input->len);
  });

  size_t result = ZSTD_decompress(output, outputLen, input->data, input->len);

  if (ZSTD_isError(result)) {
    PR_SetError(SEC_ERROR_BAD_DATA, 0);
    return rv;
  }

  *usedLen = result;
  rv = SECSuccess;
  return rv;
}

static nsresult nsSSLIOLayerSetOptions(PRFileDesc* fd, bool forSTARTTLS,
                                       bool haveProxy, const char* host,
                                       int32_t port,
                                       NSSSocketControl* infoObject) {
  if (forSTARTTLS || haveProxy) {
    if (SECSuccess != SSL_OptionSet(fd, SSL_SECURITY, false)) {
      return NS_ERROR_FAILURE;
    }
  }

  SSLVersionRange range;
  if (SSL_VersionRangeGet(fd, &range) != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  // Set TLS 1.3 compat mode.
  if (SECSuccess != SSL_OptionSet(fd, SSL_ENABLE_TLS13_COMPAT_MODE, PR_TRUE)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Error,
            ("[%p] nsSSLIOLayerSetOptions: Setting compat mode failed\n", fd));
  }

  // setting TLS max version
  uint32_t versionFlags =
      getTLSProviderFlagMaxVersion(infoObject->GetProviderTlsFlags());
  if (versionFlags) {
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("[%p] nsSSLIOLayerSetOptions: version flags %d\n", fd, versionFlags));
    if (versionFlags == kTLSProviderFlagMaxVersion10) {
      range.max = SSL_LIBRARY_VERSION_TLS_1_0;
    } else if (versionFlags == kTLSProviderFlagMaxVersion11) {
      range.max = SSL_LIBRARY_VERSION_TLS_1_1;
    } else if (versionFlags == kTLSProviderFlagMaxVersion12) {
      range.max = SSL_LIBRARY_VERSION_TLS_1_2;
    } else if (versionFlags == kTLSProviderFlagMaxVersion13) {
      range.max = SSL_LIBRARY_VERSION_TLS_1_3;
    } else {
      MOZ_LOG(gPIPNSSLog, LogLevel::Error,
              ("[%p] nsSSLIOLayerSetOptions: unknown version flags %d\n", fd,
               versionFlags));
    }
  }

  if ((infoObject->GetProviderFlags() & nsISocketProvider::BE_CONSERVATIVE) &&
      (range.max > SSL_LIBRARY_VERSION_TLS_1_2)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[%p] nsSSLIOLayerSetOptions: range.max limited to 1.2 due to "
             "BE_CONSERVATIVE flag\n",
             fd));
    range.max = SSL_LIBRARY_VERSION_TLS_1_2;
  }

  uint16_t maxEnabledVersion = range.max;
  infoObject->AdjustForTLSIntolerance(range);
  MOZ_LOG(
      gPIPNSSLog, LogLevel::Debug,
      ("[%p] nsSSLIOLayerSetOptions: using TLS version range (0x%04x,0x%04x)\n",
       fd, static_cast<unsigned int>(range.min),
       static_cast<unsigned int>(range.max)));

  // If the user has set their minimum version to something higher than what
  // we've now set the maximum to, this will result in an inconsistent version
  // range unless we fix it up. This will override their preference, but we only
  // do this for sites critical to the operation of the browser (e.g. update
  // servers) and telemetry experiments.
  if (range.min > range.max) {
    range.min = range.max;
  }

  if (SSL_VersionRangeSet(fd, &range) != SECSuccess) {
    return NS_ERROR_FAILURE;
  }
  infoObject->SetTLSVersionRange(range);

  // when adjustForTLSIntolerance tweaks the maximum version downward,
  // we tell the server using this SCSV so they can detect a downgrade attack
  if (range.max < maxEnabledVersion) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[%p] nsSSLIOLayerSetOptions: enabling TLS_FALLBACK_SCSV\n", fd));
    // Some servers will choke if we send the fallback SCSV with TLS 1.2.
    if (range.max < SSL_LIBRARY_VERSION_TLS_1_2) {
      if (SECSuccess != SSL_OptionSet(fd, SSL_ENABLE_FALLBACK_SCSV, true)) {
        return NS_ERROR_FAILURE;
      }
    }
    // tell NSS the max enabled version to make anti-downgrade effective
    if (SECSuccess != SSL_SetDowngradeCheckVersion(fd, maxEnabledVersion)) {
      return NS_ERROR_FAILURE;
    }
  }

  // Enable ECH GREASE if suitable. Has no impact if 'real' ECH is being used.
  if (range.max >= SSL_LIBRARY_VERSION_TLS_1_3 &&
      !(infoObject->GetProviderFlags() & (nsISocketProvider::BE_CONSERVATIVE |
                                          nsISocketProvider::DONT_TRY_ECH)) &&
      StaticPrefs::security_tls_ech_grease_probability()) {
    if ((RandomUint64().valueOr(0) % 100) >=
        100 - StaticPrefs::security_tls_ech_grease_probability()) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("[%p] nsSSLIOLayerSetOptions: enabling TLS ECH Grease\n", fd));
      if (SECSuccess != SSL_EnableTls13GreaseEch(fd, PR_TRUE)) {
        return NS_ERROR_FAILURE;
      }
      // ECH Padding can be between 1 and 255
      if (SECSuccess !=
          SSL_SetTls13GreaseEchSize(
              fd, std::clamp(StaticPrefs::security_tls_ech_grease_size(), 1U,
                             255U))) {
        return NS_ERROR_FAILURE;
      }
      infoObject->UpdateEchExtensionStatus(EchExtensionStatus::kGREASE);
    }
  }

  // Include a modest set of named groups in supported_groups and determine how
  // many key shares to send. Please change getKeaGroupName in
  // nsNSSCallbacks.cpp when changing the lists here.
  unsigned int additional_shares =
      StaticPrefs::security_tls_client_hello_send_p256_keyshare();
  if (StaticPrefs::security_tls_enable_kyber() &&
      range.max >= SSL_LIBRARY_VERSION_TLS_1_3 &&
      !(infoObject->GetProviderFlags() &
        (nsISocketProvider::BE_CONSERVATIVE | nsISocketProvider::IS_RETRY))) {
    const SSLNamedGroup namedGroups[] = {
        ssl_grp_kem_mlkem768x25519, ssl_grp_ec_curve25519, ssl_grp_ec_secp256r1,
        ssl_grp_ec_secp384r1,       ssl_grp_ec_secp521r1,  ssl_grp_ffdhe_2048,
        ssl_grp_ffdhe_3072};
    if (SECSuccess !=
        SSL_NamedGroupConfig(fd, namedGroups, std::size(namedGroups))) {
      return NS_ERROR_FAILURE;
    }
    additional_shares += 1;
    infoObject->WillSendMlkemShare();
  } else {
    const SSLNamedGroup namedGroups[] = {
        ssl_grp_ec_curve25519, ssl_grp_ec_secp256r1, ssl_grp_ec_secp384r1,
        ssl_grp_ec_secp521r1,  ssl_grp_ffdhe_2048,   ssl_grp_ffdhe_3072};
    // Skip the |ssl_grp_kem_mlkem768x25519| entry.
    if (SECSuccess !=
        SSL_NamedGroupConfig(fd, namedGroups, std::size(namedGroups))) {
      return NS_ERROR_FAILURE;
    }
  }

  // If additional_shares == 2, send mlkem768x25519, x25519, and p256.
  // If additional_shares == 1, send {mlkem768x25519, x25519} or {x25519, p256}.
  // If additional_shares == 0, send x25519.
  if (SECSuccess != SSL_SendAdditionalKeyShares(fd, additional_shares)) {
    return NS_ERROR_FAILURE;
  }

  // Enabling Certificate Compression Decoding mechanisms.
  if (range.max >= SSL_LIBRARY_VERSION_TLS_1_3 &&
      !(infoObject->GetProviderFlags() &
        (nsISocketProvider::BE_CONSERVATIVE | nsISocketProvider::IS_RETRY))) {
    SSLCertificateCompressionAlgorithm zlibAlg = {1, "zlib", nullptr,
                                                  zlibCertificateDecode};

    SSLCertificateCompressionAlgorithm brotliAlg = {2, "brotli", nullptr,
                                                    brotliCertificateDecode};

    SSLCertificateCompressionAlgorithm zstdAlg = {3, "zstd", nullptr,
                                                  zstdCertificateDecode};

    if (StaticPrefs::security_tls_enable_certificate_compression_zlib() &&
        SSL_SetCertificateCompressionAlgorithm(fd, zlibAlg) != SECSuccess) {
      return NS_ERROR_FAILURE;
    }

    if (StaticPrefs::security_tls_enable_certificate_compression_brotli() &&
        SSL_SetCertificateCompressionAlgorithm(fd, brotliAlg) != SECSuccess) {
      return NS_ERROR_FAILURE;
    }

    if (StaticPrefs::security_tls_enable_certificate_compression_zstd() &&
        SSL_SetCertificateCompressionAlgorithm(fd, zstdAlg) != SECSuccess) {
      return NS_ERROR_FAILURE;
    }
  }

  // NOTE: Should this list ever include ssl_sig_rsa_pss_pss_sha* (or should
  // it become possible to enable this scheme via a pref), it is required
  // to test that a Delegated Credential containing a small-modulus RSA-PSS SPKI
  // is properly rejected. NSS will not advertise PKCS1 or RSAE schemes (which
  // the |ssl_sig_rsa_pss_*| defines alias, meaning we will not currently accept
  // any RSA DC.
  if (SECSuccess !=
      SSL_SignatureSchemePrefSet(fd, sEnabledSignatureSchemes,
                                 std::size(sEnabledSignatureSchemes))) {
    return NS_ERROR_FAILURE;
  }

  bool enabled = StaticPrefs::security_ssl_enable_ocsp_stapling();
  if (SECSuccess != SSL_OptionSet(fd, SSL_ENABLE_OCSP_STAPLING, enabled)) {
    return NS_ERROR_FAILURE;
  }

  bool sctsEnabled = GetCertificateTransparencyMode() !=
                     CertVerifier::CertificateTransparencyMode::Disabled;
  if (SECSuccess !=
      SSL_OptionSet(fd, SSL_ENABLE_SIGNED_CERT_TIMESTAMPS, sctsEnabled)) {
    return NS_ERROR_FAILURE;
  }

  if (SECSuccess != SSL_OptionSet(fd, SSL_HANDSHAKE_AS_CLIENT, true)) {
    return NS_ERROR_FAILURE;
  }

#if defined(__arm__)
  if (!mozilla::supports_arm_aes()) {
    unsigned int enabledCiphers = 0;
    std::vector<uint16_t> ciphers(SSL_GetNumImplementedCiphers());

    // Returns only the enabled (reflecting prefs) ciphers, ordered
    // by their occurence in
    // https://hg.mozilla.org/projects/nss/file/a75ea4cdacd95282c6c245ebb849c25e84ccd908/lib/ssl/ssl3con.c#l87
    if (SSL_CipherSuiteOrderGet(fd, ciphers.data(), &enabledCiphers) !=
        SECSuccess) {
      return NS_ERROR_FAILURE;
    }

    // On ARM, prefer (TLS_CHACHA20_POLY1305_SHA256) over AES when hardware
    // support for AES isn't available. However, it may be disabled. If enabled,
    // it will either be element [0] or [1]*. If [0], we're done. If [1], swap
    // it with [0] (TLS_AES_128_GCM_SHA256).
    // *(assuming the compile-time order remains unchanged)
    if (enabledCiphers > 1) {
      if (ciphers[0] != TLS_CHACHA20_POLY1305_SHA256 &&
          ciphers[1] == TLS_CHACHA20_POLY1305_SHA256) {
        std::swap(ciphers[0], ciphers[1]);

        if (SSL_CipherSuiteOrderSet(fd, ciphers.data(), enabledCiphers) !=
            SECSuccess) {
          return NS_ERROR_FAILURE;
        }
      }
    }
  }
#endif

  // Set the Peer ID so that SSL proxy connections work properly and to
  // separate anonymous and/or private browsing connections.
  nsAutoCString peerId;
  infoObject->GetPeerId(peerId);
  if (SECSuccess != SSL_SetSockPeerID(fd, peerId.get())) {
    return NS_ERROR_FAILURE;
  }

  uint32_t flags = infoObject->GetProviderFlags();
  if (flags & nsISocketProvider::NO_PERMANENT_STORAGE) {
    if (SECSuccess != SSL_OptionSet(fd, SSL_ENABLE_SESSION_TICKETS, false) ||
        SECSuccess != SSL_OptionSet(fd, SSL_NO_CACHE, true)) {
      return NS_ERROR_FAILURE;
    }
  }

  return NS_OK;
}

SECStatus StoreResumptionToken(PRFileDesc* fd, const PRUint8* resumptionToken,
                               unsigned int len, void* ctx) {
  PRIntn val;
  if (SSL_OptionGet(fd, SSL_ENABLE_SESSION_TICKETS, &val) != SECSuccess ||
      val == 0) {
    return SECFailure;
  }

  NSSSocketControl* infoObject = (NSSSocketControl*)ctx;
  if (!infoObject) {
    return SECFailure;
  }

  nsAutoCString peerId;
  infoObject->GetPeerId(peerId);
  if (NS_FAILED(
          net::SSLTokensCache::Put(peerId, resumptionToken, len, infoObject))) {
    return SECFailure;
  }

  return SECSuccess;
}

nsresult nsSSLIOLayerAddToSocket(int32_t family, const char* host, int32_t port,
                                 nsIProxyInfo* proxy,
                                 const OriginAttributes& originAttributes,
                                 PRFileDesc* fd,
                                 nsITLSSocketControl** tlsSocketControl,
                                 bool forSTARTTLS, uint32_t providerFlags,
                                 uint32_t providerTlsFlags) {
  RefPtr<nsSSLIOLayerHelpers> sslIOLayerHelpers;
  if (providerTlsFlags) {
    sslIOLayerHelpers =
        new nsSSLIOLayerHelpers(PublicOrPrivate::Public, providerTlsFlags);
    sslIOLayerHelpers->Init();
  } else {
    bool isPrivate = providerFlags & nsISocketProvider::NO_PERMANENT_STORAGE ||
                     originAttributes.IsPrivateBrowsing();
    sslIOLayerHelpers =
        isPrivate ? PrivateSSLIOLayerHelpers() : PublicSSLIOLayerHelpers();
  }

  RefPtr<NSSSocketControl> infoObject(new NSSSocketControl(
      nsDependentCString(host), port, sslIOLayerHelpers.forget(), providerFlags,
      providerTlsFlags));
  if (!infoObject) {
    return NS_ERROR_FAILURE;
  }

  infoObject->SetForSTARTTLS(forSTARTTLS);
  infoObject->SetOriginAttributes(originAttributes);

  bool haveProxy = false;
  bool haveHTTPSProxy = false;
  if (proxy) {
    nsAutoCString proxyHost;
    nsresult rv = proxy->GetHost(proxyHost);
    if (NS_FAILED(rv)) {
      return rv;
    }
    haveProxy = !proxyHost.IsEmpty();
    nsAutoCString type;
    haveHTTPSProxy = haveProxy && NS_SUCCEEDED(proxy->GetType(type)) &&
                     type.EqualsLiteral("https");
  }

  // A plaintext observer shim is inserted so we can observe some protocol
  // details without modifying nss
  PRFileDesc* plaintextLayer =
      PR_CreateIOLayerStub(nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity,
                           &nsSSLIOLayerHelpers::nsSSLPlaintextLayerMethods);
  if (!plaintextLayer) {
    return NS_ERROR_FAILURE;
  }
  plaintextLayer->secret = (PRFilePrivate*)infoObject.get();
  if (PR_PushIOLayer(fd, PR_TOP_IO_LAYER, plaintextLayer) != PR_SUCCESS) {
    plaintextLayer->dtor(plaintextLayer);
    return NS_ERROR_FAILURE;
  }
  auto plaintextLayerCleanup = MakeScopeExit([&fd] {
    // Note that PR_*IOLayer operations may modify the stack of fds, so a
    // previously-valid pointer may no longer point to what we think it points
    // to after calling PR_PopIOLayer. We must operate on the pointer returned
    // by PR_PopIOLayer.
    PRFileDesc* plaintextLayer =
        PR_PopIOLayer(fd, nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity);
    if (plaintextLayer) {
      plaintextLayer->dtor(plaintextLayer);
    }
  });

  PRFileDesc* sslSock =
      nsSSLIOLayerImportFD(fd, infoObject, host, haveHTTPSProxy);
  if (!sslSock) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = nsSSLIOLayerSetOptions(sslSock, forSTARTTLS, haveProxy, host,
                                       port, infoObject);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Now, layer ourselves on top of the SSL socket...
  PRFileDesc* layer =
      PR_CreateIOLayerStub(nsSSLIOLayerHelpers::nsSSLIOLayerIdentity,
                           &nsSSLIOLayerHelpers::nsSSLIOLayerMethods);
  if (!layer) {
    return NS_ERROR_FAILURE;
  }
  // Give the layer an owning reference to the NSSSocketControl.
  // This is the simplest way to prevent the layer from outliving the
  // NSSSocketControl (otherwise, the layer could potentially use it in
  // nsSSLIOLayerClose after it has been released).
  // nsSSLIOLayerClose takes the owning reference when the underlying fd gets
  // closed. If the fd never gets closed (as in, leaks), the NSSSocketControl
  // will also leak.
  layer->secret = (PRFilePrivate*)do_AddRef(infoObject).take();

  if (PR_PushIOLayer(sslSock, PR_GetLayersIdentity(sslSock), layer) !=
      PR_SUCCESS) {
    layer->dtor(layer);
    return NS_ERROR_FAILURE;
  }
  auto layerCleanup = MakeScopeExit([&fd] {
    PRFileDesc* layer =
        PR_PopIOLayer(fd, nsSSLIOLayerHelpers::nsSSLIOLayerIdentity);
    if (layer) {
      layer->dtor(layer);
    }
  });

  // We are going use a clear connection first //
  if (forSTARTTLS || haveProxy) {
    infoObject->SetHandshakeNotPending();
  }

  rv = infoObject->SetResumptionTokenFromExternalCache(sslSock);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (SSL_SetResumptionTokenCallback(sslSock, &StoreResumptionToken,
                                     infoObject) != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("[%p] Socket set up", (void*)sslSock));

  (void)infoObject->SetFileDescPtr(sslSock);
  layerCleanup.release();
  plaintextLayerCleanup.release();
  *tlsSocketControl = infoObject.forget().take();
  return NS_OK;
}

extern "C" {

const uint8_t kIPCClientCertsObjectTypeCert = 1;
const uint8_t kIPCClientCertsObjectTypeRSAKey = 2;
const uint8_t kIPCClientCertsObjectTypeECKey = 3;

// This function is provided to the IPC client certs module so it can cause the
// parent process to find certificates and keys and send identifying
// information about them over IPC.
void DoFindObjects(FindObjectsCallback cb, void* ctx) {
  net::SocketProcessChild* socketChild =
      net::SocketProcessChild::GetSingleton();
  if (!socketChild) {
    return;
  }

  RefPtr<IPCClientCertsChild> ipcClientCertsActor(
      socketChild->GetIPCClientCertsActor());
  if (!ipcClientCertsActor) {
    return;
  }
  nsTArray<IPCClientCertObject> objects;
  if (!ipcClientCertsActor->SendFindObjects(&objects)) {
    return;
  }
  for (const auto& object : objects) {
    switch (object.type()) {
      case IPCClientCertObject::TECKey:
        cb(kIPCClientCertsObjectTypeECKey, object.get_ECKey().params().Length(),
           object.get_ECKey().params().Elements(),
           object.get_ECKey().cert().Length(),
           object.get_ECKey().cert().Elements(), ctx);
        break;
      case IPCClientCertObject::TRSAKey:
        cb(kIPCClientCertsObjectTypeRSAKey,
           object.get_RSAKey().modulus().Length(),
           object.get_RSAKey().modulus().Elements(),
           object.get_RSAKey().cert().Length(),
           object.get_RSAKey().cert().Elements(), ctx);
        break;
      case IPCClientCertObject::TCertificate:
        cb(kIPCClientCertsObjectTypeCert,
           object.get_Certificate().der().Length(),
           object.get_Certificate().der().Elements(), 0, nullptr, ctx);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("unhandled IPCClientCertObject type");
        break;
    }
  }
}

// This function is provided to the IPC client certs module so it can cause the
// parent process to sign the given data using the key corresponding to the
// given certificate, using the given parameters.
void DoSign(size_t cert_len, const uint8_t* cert, size_t data_len,
            const uint8_t* data, size_t params_len, const uint8_t* params,
            SignCallback cb, void* ctx) {
  net::SocketProcessChild* socketChild =
      net::SocketProcessChild::GetSingleton();
  if (!socketChild) {
    return;
  }

  RefPtr<IPCClientCertsChild> ipcClientCertsActor(
      socketChild->GetIPCClientCertsActor());
  if (!ipcClientCertsActor) {
    return;
  }
  ByteArray certBytes(nsTArray<uint8_t>(cert, cert_len));
  ByteArray dataBytes(nsTArray<uint8_t>(data, data_len));
  ByteArray paramsBytes(nsTArray<uint8_t>(params, params_len));
  ByteArray signature;
  if (!ipcClientCertsActor->SendSign(certBytes, dataBytes, paramsBytes,
                                     &signature)) {
    return;
  }
  cb(signature.data().Length(), signature.data().Elements(), ctx);
}

#ifdef MOZ_WIDGET_ANDROID
// Similar to `DoFindObjects`, this function implements searching for client
// authentication certificates on Android. When a TLS server requests a client
// auth certificate, the backend will forward that request to the frontend,
// which calls KeyChain.choosePrivateKeyAlias. The user can choose a
// certificate, which causes it to become available for use. The
// `ClientAuthCertificateManager` singleton keeps track of these certificates.
// This function is called by osclientcerts when the backend looks for new
// certificates and keys. It gets a list of all known client auth certificates
// from `ClientAuthCertificateManager` and returns them via the callback.
void AndroidDoFindObjects(FindObjectsCallback cb, void* ctx) {
  if (!jni::IsAvailable()) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("AndroidDoFindObjects: JNI not available"));
    return;
  }
  jni::ObjectArray::LocalRef clientAuthCertificates =
      java::ClientAuthCertificateManager::GetClientAuthCertificates();
  for (size_t i = 0; i < clientAuthCertificates->Length(); i++) {
    java::ClientAuthCertificateManager::ClientAuthCertificate::LocalRef
        clientAuthCertificate = clientAuthCertificates->GetElement(i);
    jni::ByteArray::LocalRef der = clientAuthCertificate->GetCertificateBytes();
    jni::ByteArray::LocalRef keyParameters =
        clientAuthCertificate->GetKeyParameters();
    cb(kIPCClientCertsObjectTypeCert, der->Length(),
       reinterpret_cast<uint8_t*>(der->GetElements().Elements()), 0, nullptr,
       ctx);
    cb(clientAuthCertificate->GetType(), keyParameters->Length(),
       reinterpret_cast<uint8_t*>(keyParameters->GetElements().Elements()),
       der->Length(), reinterpret_cast<uint8_t*>(der->GetElements().Elements()),
       ctx);
    jni::ObjectArray::LocalRef issuersBytes =
        clientAuthCertificate->GetIssuersBytes();
    if (issuersBytes) {
      for (size_t i = 0; i < issuersBytes->Length(); i++) {
        jni::ByteArray::LocalRef issuer = issuersBytes->GetElement(i);
        cb(kIPCClientCertsObjectTypeCert, issuer->Length(),
           reinterpret_cast<uint8_t*>(issuer->GetElements().Elements()), 0,
           nullptr, ctx);
      }
    }
  }
}

// Similar to `DoSign`, this function implements signing for client
// authentication certificates on Android. `ClientAuthCertificateManager` keeps
// track of any available client auth certificates and does the actual work of
// signing - this function just passes in the appropriate parameters.
void AndroidDoSign(size_t certLen, const uint8_t* cert, size_t dataLen,
                   const uint8_t* data, const char* algorithm, SignCallback cb,
                   void* ctx) {
  if (!jni::IsAvailable()) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("AndroidDoSign: JNI not available"));
    return;
  }
  jni::ByteArray::LocalRef certBytes =
      jni::ByteArray::New(reinterpret_cast<const int8_t*>(cert), certLen);
  jni::ByteArray::LocalRef dataBytes =
      jni::ByteArray::New(reinterpret_cast<const int8_t*>(data), dataLen);
  jni::String::LocalRef algorithmStr = jni::StringParam(algorithm);
  jni::ByteArray::LocalRef signature = java::ClientAuthCertificateManager::Sign(
      certBytes, dataBytes, algorithmStr);
  if (signature) {
    cb(signature->Length(),
       reinterpret_cast<const uint8_t*>(signature->GetElements().Elements()),
       ctx);
  }
}
#endif  // MOZ_WIDGET_ANDROID
}  // extern "C"
