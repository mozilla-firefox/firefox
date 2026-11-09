/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// During certificate authentication, we call CertVerifier::VerifySSLServerCert.
// This function may make zero or more HTTP requests (e.g. to gather revocation
// information). Our fetching logic for these requests processes them on the
// socket transport service thread.
//
// Because the connection for which we are verifying the certificate is
// happening on the socket transport thread, if our cert auth hook were to call
// VerifySSLServerCert directly, there would be a deadlock: VerifySSLServerCert
// would cause an event to be asynchronously posted to the socket transport
// thread, and then it would block the socket transport thread waiting to be
// notified of the HTTP response. However, the HTTP request would never actually
// be processed because the socket transport thread would be blocked and so it
// wouldn't be able process HTTP requests.
//
// Consequently, when we are asked to verify a certificate, we must always call
// VerifySSLServerCert on another thread. To accomplish this, our auth cert hook
// dispatches a SSLServerCertVerificationJob to a pool of background threads,
// and then immediately returns SECWouldBlock to libssl. These jobs are where
// VerifySSLServerCert is actually called.
//
// When our auth cert hook returns SECWouldBlock, libssl will carry on the
// handshake while we validate the certificate. This will free up the socket
// transport thread so that HTTP requests--including the OCSP requests needed
// for cert verification as mentioned above--can be processed.
//
// Once VerifySSLServerCert returns, the cert verification job dispatches a
// SSLServerCertVerificationResult to the socket transport thread; the
// SSLServerCertVerificationResult will notify libssl that the certificate
// authentication is complete. Once libssl is notified that the authentication
// is complete, it will continue the TLS handshake (if it hasn't already
// finished) and it will begin allowing us to send/receive data on the
// connection.
//
// Timeline of events (for connections managed by the socket transport service):
//
//    * libssl calls SSLServerCertVerificationJob::Dispatch on the socket
//      transport thread.
//    * SSLServerCertVerificationJob::Dispatch queues a job
//      (instance of SSLServerCertVerificationJob) to its background thread
//      pool and returns.
//    * One of the background threads calls CertVerifier::VerifySSLServerCert,
//      which may enqueue some HTTP request(s) onto the socket transport thread,
//      and then blocks that background thread waiting for the responses and/or
//      timeouts or errors for those requests.
//    * Once those HTTP responses have all come back or failed, the
//      CertVerifier::VerifySSLServerCert function returns a result indicating
//      that the validation succeeded or failed.
//    * If the validation succeeded, then a SSLServerCertVerificationResult
//      event is posted to the socket transport thread, and the cert
//      verification thread becomes free to verify other certificates.
//    * Otherwise, we do cert override processing to see if the validation
//      error can be convered by override rules. The result of this processing
//      is similarly dispatched in a SSLServerCertVerificationResult.
//    * The SSLServerCertVerificationResult event will either wake up the
//      socket (using SSL_AuthCertificateComplete) if validation succeeded or
//      there was an error override, or it will set an error flag so that the
//      next I/O operation on the socket will fail, causing the socket transport
//      thread to close the connection.
//
// SSLServerCertVerificationResult must be dispatched to the socket transport
// thread because we must only call SSL_* functions on the socket transport
// thread since they may do I/O, because many parts of NSSSocketControl and the
// PSM NSS I/O layer are not thread-safe, and because we need the event to
// interrupt the PR_Poll that may waiting for I/O on the socket for which we
// are validating the cert.
//
#include "SSLServerCertVerification.h"

#include "CertVerifier.h"
#include "CryptoTask.h"
#include "ExtendedValidation.h"
#include "NSSCertDBTrustDomain.h"
#include "NSSSocketControl.h"
#include "RootCertificateTelemetryUtils.h"
#include "ScopedNSSTypes.h"
#include "SharedCertVerifier.h"
#include "cert.h"
#include "mozilla/Assertions.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"
#include "mozpkix/pkix.h"
#include "mozpkix/pkixcheck.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsICertOverrideService.h"
#include "nsIPublicKeyPinningService.h"
#include "nsISiteSecurityService.h"
#include "nsISocketProvider.h"
#include "nsNSSCertificate.h"
#include "nsNSSComponent.h"
#include "nsNSSIOLayer.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsThreadPool.h"
#include "nsURLHelper.h"
#include "nsXPCOMCIDInternal.h"
#include "secerr.h"
#include "secport.h"
#include "ssl.h"
#include "sslerr.h"
#include "sslexp.h"

extern mozilla::LazyLogModule gPIPNSSLog;

using namespace mozilla::pkix;

namespace mozilla {
namespace psm {

// do not use a nsCOMPtr to avoid static initializer/destructor
nsIThreadPool* gCertVerificationThreadPool = nullptr;

// Called when the socket transport thread starts, to initialize the SSL cert
// verification thread pool. By tying the thread pool startup/shutdown directly
// to the STS thread's lifetime, we ensure that they are *always* available for
// SSL connections and that there are no races during startup and especially
// shutdown. (Previously, we have had multiple problems with races in PSM
// background threads, and the race-prevention/shutdown logic used there is
// brittle. Since this service is critical to things like downloading updates,
// we take no chances.) Also, by doing things this way, we avoid the need for
// locks, since gCertVerificationThreadPool is only ever accessed on the socket
// transport thread.
void InitializeSSLServerCertVerificationThreads() {
  // TODO: tuning, make parameters preferences
  gCertVerificationThreadPool = new nsThreadPool();
  NS_ADDREF(gCertVerificationThreadPool);

  (void)gCertVerificationThreadPool->SetThreadLimit(5);
  (void)gCertVerificationThreadPool->SetIdleThreadLimit(1);
  (void)gCertVerificationThreadPool->SetIdleThreadMaximumTimeout(30 * 1000);
  (void)gCertVerificationThreadPool->SetIdleThreadGraceTimeout(500);
  (void)gCertVerificationThreadPool->SetName("SSL Cert"_ns);
}

// Called when the socket transport thread finishes, to destroy the thread
// pool. Since the socket transport service has stopped processing events, it
// will not attempt any more SSL I/O operations, so it is clearly safe to shut
// down the SSL cert verification infrastructure. Also, the STS will not
// dispatch many SSL verification result events at this point, so any pending
// cert verifications will (correctly) fail at the point they are dispatched.
//
// The other shutdown race condition that is possible is a race condition with
// shutdown of the nsNSSComponent service. We use the
// nsNSSShutdownPreventionLock where needed (not here) to prevent that.
void StopSSLServerCertVerificationThreads() {
  if (gCertVerificationThreadPool) {
    gCertVerificationThreadPool->Shutdown();
    NS_RELEASE(gCertVerificationThreadPool);
  }
}

// A probe value of 1 means "no error".
uint32_t MapOverridableErrorToProbeValue(PRErrorCode errorCode) {
  switch (errorCode) {
    case SEC_ERROR_UNKNOWN_ISSUER:
      return 2;
    case SEC_ERROR_CA_CERT_INVALID:
      return 3;
    case SEC_ERROR_UNTRUSTED_ISSUER:
      return 4;
    case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
      return 5;
    case SEC_ERROR_UNTRUSTED_CERT:
      return 6;
    case SEC_ERROR_INADEQUATE_KEY_USAGE:
      return 7;
    case SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED:
      return 8;
    case SSL_ERROR_BAD_CERT_DOMAIN:
      return 9;
    case SEC_ERROR_EXPIRED_CERTIFICATE:
      return 10;
    case mozilla::pkix::MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY:
      return 11;
    case mozilla::pkix::MOZILLA_PKIX_ERROR_V1_CERT_USED_AS_CA:
      return 12;
    case mozilla::pkix::MOZILLA_PKIX_ERROR_INADEQUATE_KEY_SIZE:
      return 13;
    case mozilla::pkix::MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE:
      return 14;
    case mozilla::pkix::MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE:
      return 15;
    case SEC_ERROR_INVALID_TIME:
      return 16;
    case mozilla::pkix::MOZILLA_PKIX_ERROR_EMPTY_ISSUER_NAME:
      return 17;
    // mozilla::pkix::MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED was
    // 18
    case mozilla::pkix::MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT:
      return 19;
    case mozilla::pkix::MOZILLA_PKIX_ERROR_MITM_DETECTED:
      return 20;
    case mozilla::pkix::
        MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY:
      return 21;
  }
  NS_WARNING(
      "Unknown certificate error code. Does MapOverridableErrorToProbeValue "
      "handle everything in CategorizeCertificateError?");
  return 0;
}

static uint32_t MapCertErrorToProbeValue(PRErrorCode errorCode) {
  uint32_t probeValue;
  switch (errorCode) {
    // see security/pkix/include/pkix/Result.h
#define MOZILLA_PKIX_MAP(name, value, nss_name) \
  case nss_name:                                \
    probeValue = value;                         \
    break;
    MOZILLA_PKIX_MAP_LIST
#undef MOZILLA_PKIX_MAP
    default:
      return 0;
  }

  // Since FATAL_ERROR_FLAG is 0x800, fatal error values are much larger than
  // non-fatal error values. To conserve space, we remap these so they start at
  // (decimal) 90 instead of 0x800. Currently there are ~50 non-fatal errors
  // mozilla::pkix might return, so saving space for 90 should be sufficient
  // (similarly, there are 4 fatal errors, so saving space for 10 should also
  // be sufficient).
  static_assert(
      FATAL_ERROR_FLAG == 0x800,
      "mozilla::pkix::FATAL_ERROR_FLAG is not what we were expecting");
  if (probeValue & FATAL_ERROR_FLAG) {
    probeValue ^= FATAL_ERROR_FLAG;
    probeValue += 90;
  }
  return probeValue;
}

// If the given PRErrorCode is an overridable certificate error, return which
// category (trust, time, domain mismatch) it falls in. If it is not
// overridable, return Nothing.
Maybe<nsITransportSecurityInfo::OverridableErrorCategory>
CategorizeCertificateError(PRErrorCode certificateError) {
  switch (certificateError) {
    case SEC_ERROR_CA_CERT_INVALID:
    case SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED:
    case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
    case SEC_ERROR_UNKNOWN_ISSUER:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_EMPTY_ISSUER_NAME:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_INADEQUATE_KEY_SIZE:
    case mozilla::pkix::
        MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_MITM_DETECTED:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_V1_CERT_USED_AS_CA:
      return Some(
          nsITransportSecurityInfo::OverridableErrorCategory::ERROR_TRUST);

    case SSL_ERROR_BAD_CERT_DOMAIN:
      return Some(
          nsITransportSecurityInfo::OverridableErrorCategory::ERROR_DOMAIN);

    case SEC_ERROR_EXPIRED_CERTIFICATE:
    case SEC_ERROR_INVALID_TIME:
    case mozilla::pkix::MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE:
      return Some(
          nsITransportSecurityInfo::OverridableErrorCategory::ERROR_TIME);

    default:
      break;
  }
  return Nothing();
}

// Helper function to determine if overrides are allowed for this host.
// Overrides are not allowed for known HSTS hosts or hosts with pinning
// information. However, IP addresses can never be HSTS hosts and don't have
// pinning information.
static nsresult OverrideAllowedForHost(
    uint64_t aPtrForLog, const nsACString& aHostname,
    const OriginAttributes& aOriginAttributes, /*out*/ bool& aOverrideAllowed) {
  aOverrideAllowed = false;

  // If this is an IP address, overrides are allowed, because an IP address is
  // never an HSTS host. nsISiteSecurityService takes this into account
  // already, but the real problem here is that calling NS_NewURI with an IPv6
  // address fails. We do this to avoid that. A more comprehensive fix would be
  // to have Necko provide an nsIURI to PSM and to use that here (and
  // everywhere). However, that would be a wide-spanning change.
  if (net_IsValidIPv6Addr(aHostname)) {
    aOverrideAllowed = true;
    return NS_OK;
  }

  // If this is an HTTP Strict Transport Security host or a pinned host and the
  // certificate is bad, don't allow overrides (RFC 6797 section 12.1).
  bool strictTransportSecurityEnabled = false;
  bool isStaticallyPinned = false;
  nsCOMPtr<nsISiteSecurityService> sss(do_GetService(NS_SSSERVICE_CONTRACTID));
  if (!sss) {
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("[0x%" PRIx64 "] Couldn't get nsISiteSecurityService to check HSTS",
         aPtrForLog));
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), "https://"_ns + aHostname);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[0x%" PRIx64 "] Creating new URI failed", aPtrForLog));
    return rv;
  }

  rv =
      sss->IsSecureURI(uri, aOriginAttributes, &strictTransportSecurityEnabled);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[0x%" PRIx64 "] checking for HSTS failed", aPtrForLog));
    return rv;
  }

  nsCOMPtr<nsIPublicKeyPinningService> pkps =
      do_GetService(NS_PKPSERVICE_CONTRACTID, &rv);
  if (!pkps) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[0x%" PRIx64
             "] Couldn't get nsIPublicKeyPinningService to check pinning",
             aPtrForLog));
    return NS_ERROR_FAILURE;
  }
  rv = pkps->HostHasPins(uri, &isStaticallyPinned);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[0x%" PRIx64 "] checking for static pin failed", aPtrForLog));
    return rv;
  }

  aOverrideAllowed = !strictTransportSecurityEnabled && !isStaticallyPinned;
  return NS_OK;
}

// This function assumes that we will only use the SPDY connection coalescing
// feature on connections where we have negotiated SPDY using NPN. If we ever
// talk SPDY without having negotiated it with SPDY, this code will give wrong
// and perhaps unsafe results.
//
// Returns SECSuccess on the initial handshake of all connections, on
// renegotiations for any connections where we did not negotiate SPDY, or on any
// SPDY connection where the server's certificate did not change.
//
// Prohibit changing the server cert only if we negotiated SPDY,
// in order to support SPDY's cross-origin connection pooling.
static SECStatus BlockServerCertChangeForSpdy(
    NSSSocketControl* socketControl, const nsTArray<uint8_t>& serverCertDER) {
  if (!socketControl->IsHandshakeCompleted()) {
    // first handshake on this connection, not a
    // renegotiation.
    return SECSuccess;
  }

  // Filter out sockets that did not neogtiate SPDY via NPN
  nsCOMPtr<nsITransportSecurityInfo> securityInfo;
  nsresult rv = socketControl->GetSecurityInfo(getter_AddRefs(securityInfo));
  MOZ_ASSERT(NS_SUCCEEDED(rv), "GetSecurityInfo() failed during renegotiation");
  if (NS_FAILED(rv) || !securityInfo) {
    PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
    return SECFailure;
  }
  nsAutoCString negotiatedNPN;
  rv = securityInfo->GetNegotiatedNPN(negotiatedNPN);
  MOZ_ASSERT(NS_SUCCEEDED(rv),
             "GetNegotiatedNPN() failed during renegotiation");

  if (NS_SUCCEEDED(rv) && !StringBeginsWith(negotiatedNPN, "spdy/"_ns)) {
    return SECSuccess;
  }
  // If GetNegotiatedNPN() failed we will assume spdy for safety's safe
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("BlockServerCertChangeForSpdy failed GetNegotiatedNPN() call."
             " Assuming spdy."));
  }

  // Check to see if the cert has actually changed
  nsCOMPtr<nsIX509Cert> cert(socketControl->GetServerCert());
  if (!cert) {
    PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
    return SECFailure;
  }
  nsTArray<uint8_t> certDER;
  if (NS_FAILED(cert->GetRawDER(certDER))) {
    PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
    return SECFailure;
  }
  if (certDER == serverCertDER) {
    return SECSuccess;
  }

  // Report an error - changed cert is confirmed
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("SPDY refused to allow new cert during renegotiation"));
  PR_SetError(SSL_ERROR_RENEGOTIATION_NOT_ALLOWED, 0);
  return SECFailure;
}

void GatherTelemetryForSingleSCT(const ct::VerifiedSCT& verifiedSct) {
  // See scts_verification_status in metrics.yaml.
  uint32_t verificationStatus = 0;
  switch (verifiedSct.logState) {
    case ct::CTLogState::Admissible:
      verificationStatus = 1;
      break;
    case ct::CTLogState::Retired:
      verificationStatus = 5;
      break;
  }
  glean::ssl::scts_verification_status.AccumulateSingleSample(
      verificationStatus);
}

void GatherCertificateTransparencyTelemetry(
    const nsTArray<uint8_t>& rootCert,
    const CertificateTransparencyInfo& info) {
  if (!info.enabled) {
    // No telemetry is gathered when CT is disabled.
    return;
  }

  for (const ct::VerifiedSCT& sct : info.verifyResult.verifiedScts) {
    GatherTelemetryForSingleSCT(sct);
  }

  // See scts_verification_status in metrics.yaml.
  for (size_t i = 0; i < info.verifyResult.decodingErrors; ++i) {
    glean::ssl::scts_verification_status.AccumulateSingleSample(0);
  }
  for (size_t i = 0; i < info.verifyResult.sctsFromUnknownLogs; ++i) {
    glean::ssl::scts_verification_status.AccumulateSingleSample(2);
  }
  for (size_t i = 0; i < info.verifyResult.sctsWithInvalidSignatures; ++i) {
    glean::ssl::scts_verification_status.AccumulateSingleSample(3);
  }
  for (size_t i = 0; i < info.verifyResult.sctsWithInvalidTimestamps; ++i) {
    glean::ssl::scts_verification_status.AccumulateSingleSample(4);
  }
  for (size_t i = 0; i < info.verifyResult.sctsWithDistrustedTimestamps; ++i) {
    glean::ssl::scts_verification_status.AccumulateSingleSample(6);
  }

  // See scts_origin in metrics.yaml.
  for (size_t i = 0; i < info.verifyResult.embeddedSCTs; ++i) {
    glean::ssl::scts_origin.AccumulateSingleSample(1);
  }
  for (size_t i = 0; i < info.verifyResult.sctsFromTLSHandshake; ++i) {
    glean::ssl::scts_origin.AccumulateSingleSample(2);
  }
  for (size_t i = 0; i < info.verifyResult.sctsFromOCSP; ++i) {
    glean::ssl::scts_origin.AccumulateSingleSample(3);
  }

  // Handle the histogram of SCTs counts.
  uint32_t sctsCount =
      static_cast<uint32_t>(info.verifyResult.verifiedScts.size());
  // Note that sctsCount can also be 0 in case we've received SCT binary data,
  // but it failed to parse (e.g. due to unsupported CT protocol version).
  glean::ssl::scts_per_connection.AccumulateSingleSample(sctsCount);

  uint32_t sctsFromTiledLogs = 0;
  for (const auto& verifiedSCT : info.verifyResult.verifiedScts) {
    if (verifiedSCT.logFormat == ct::CTLogFormat::Tiled) {
      sctsFromTiledLogs++;
    }
  }
  glean::ssl::scts_from_tiled_logs_per_connection.AccumulateSingleSample(
      sctsFromTiledLogs);

  // Report CT Policy compliance by CA.
  if (info.policyCompliance.isSome() &&
      *info.policyCompliance != ct::CTPolicyCompliance::Compliant) {
    int32_t binId = RootCABinNumber(rootCert);
    if (binId != ROOT_CERTIFICATE_HASH_FAILURE) {
      glean::ssl::ct_policy_non_compliant_connections_by_ca_2
          .AccumulateSingleSample(binId);
    }
  }
}

// This function collects telemetry about certs. It will be called on one of
// CertVerificationThread.
static void CollectCertTelemetry(
    mozilla::pkix::Result aCertVerificationResult, EVStatus aEVStatus,
    CertVerifier::OCSPStaplingStatus aOcspStaplingStatus,
    KeySizeStatus aKeySizeStatus,
    const PinningTelemetryInfo& aPinningTelemetryInfo,
    const nsTArray<nsTArray<uint8_t>>& aBuiltCertChain,
    const CertificateTransparencyInfo& aCertificateTransparencyInfo,
    const IssuerSources& issuerSources) {
  uint32_t evStatus = (aCertVerificationResult != Success) ? 0  // 0 = Failure
                      : (aEVStatus != EVStatus::EV)        ? 1  // 1 = DV
                                                           : 2;        // 2 = EV
  glean::cert::ev_status.AccumulateSingleSample(evStatus);

  if (aOcspStaplingStatus != CertVerifier::OCSP_STAPLING_NEVER_CHECKED) {
    glean::ssl::ocsp_stapling.AccumulateSingleSample(aOcspStaplingStatus);
  }

  if (aKeySizeStatus != KeySizeStatus::NeverChecked) {
    glean::cert::chain_key_size_status.AccumulateSingleSample(
        static_cast<uint32_t>(aKeySizeStatus));
  }

  if (aPinningTelemetryInfo.accumulateForRoot) {
    glean::cert_pinning::failures_by_ca_2.AccumulateSingleSample(
        aPinningTelemetryInfo.rootBucket);
  }

  if (aPinningTelemetryInfo.accumulateResult) {
    if (aPinningTelemetryInfo.isMoz) {
      if (aPinningTelemetryInfo.testMode) {
        glean::cert_pinning::moz_test_results_by_host.AccumulateSingleSample(
            aPinningTelemetryInfo.certPinningResultBucket);
      } else {
        glean::cert_pinning::moz_results_by_host.AccumulateSingleSample(
            aPinningTelemetryInfo.certPinningResultBucket);
      }
    } else {
      if (aPinningTelemetryInfo.testMode) {
        glean::cert_pinning::test_results
            .EnumGet(static_cast<glean::cert_pinning::TestResultsLabel>(
                aPinningTelemetryInfo.certPinningResultBucket))
            .Add();
      } else {
        glean::cert_pinning::results
            .EnumGet(static_cast<glean::cert_pinning::ResultsLabel>(
                aPinningTelemetryInfo.certPinningResultBucket))
            .Add();
      }
    }
  }

  if (aCertVerificationResult == Success && aBuiltCertChain.Length() > 0) {
    const nsTArray<uint8_t>& rootCert = aBuiltCertChain.LastElement();
    int32_t binId = RootCABinNumber(rootCert);
    if (binId != ROOT_CERTIFICATE_HASH_FAILURE) {
      glean::cert::validation_success_by_ca_2.AccumulateSingleSample(binId);
    }

    mozilla::glean::tls::certificate_verifications.Add(1);
    if (issuerSources.contains(IssuerSource::TLSHandshake)) {
      mozilla::glean::verification_used_cert_from::tls_handshake.AddToNumerator(
          1);
    }
    if (issuerSources.contains(IssuerSource::PreloadedIntermediates)) {
      mozilla::glean::verification_used_cert_from::preloaded_intermediates
          .AddToNumerator(1);
    }
    if (issuerSources.contains(IssuerSource::ThirdPartyCertificates)) {
      mozilla::glean::verification_used_cert_from::third_party_certificates
          .AddToNumerator(1);
    }
    if (issuerSources.contains(IssuerSource::NSSCertDB)) {
      mozilla::glean::verification_used_cert_from::nss_cert_db.AddToNumerator(
          1);
    }
    if (issuerSources.contains(IssuerSource::BuiltInRootsModule)) {
      mozilla::glean::verification_used_cert_from::built_in_roots_module
          .AddToNumerator(1);
    }
    if (issuerSources.contains(IssuerSource::PKCS11Module)) {
      mozilla::glean::verification_used_cert_from::pkcs11_module.AddToNumerator(
          1);
    }
  }

  if ((aCertVerificationResult == Success ||
       aCertVerificationResult ==
           Result::ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY) &&
      aBuiltCertChain.Length() > 0) {
    const nsTArray<uint8_t>& rootCert = aBuiltCertChain.LastElement();
    GatherCertificateTransparencyTelemetry(rootCert,
                                           aCertificateTransparencyInfo);
  }
}

// Note: Takes ownership of |peerCertChain| if SECSuccess is not returned.
Result AuthCertificate(
    CertVerifier& certVerifier, void* aPinArg,
    const nsTArray<uint8_t>& certBytes,
    const nsTArray<nsTArray<uint8_t>>& peerCertChain,
    const nsACString& aHostName, const OriginAttributes& aOriginAttributes,
    const Maybe<nsTArray<uint8_t>>& stapledOCSPResponse,
    const Maybe<nsTArray<uint8_t>>& sctsFromTLSExtension,
    const Maybe<DelegatedCredentialInfo>& dcInfo, uint32_t providerFlags,
    Time time, uint32_t certVerifierFlags,
    /*out*/ nsTArray<nsTArray<uint8_t>>& builtCertChain,
    /*out*/ EVStatus& evStatus,
    /*out*/ CertificateTransparencyInfo& certificateTransparencyInfo,
    /*out*/ bool& aIsBuiltCertChainRootBuiltInRoot,
    /*out*/ bool& aMadeOCSPRequests) {
  CertVerifier::OCSPStaplingStatus ocspStaplingStatus =
      CertVerifier::OCSP_STAPLING_NEVER_CHECKED;
  KeySizeStatus keySizeStatus = KeySizeStatus::NeverChecked;
  PinningTelemetryInfo pinningTelemetryInfo;

  nsTArray<nsTArray<uint8_t>> peerCertsBytes;
  // Don't include the end-entity certificate.
  if (!peerCertChain.IsEmpty()) {
    std::transform(
        peerCertChain.cbegin() + 1, peerCertChain.cend(),
        MakeBackInserter(peerCertsBytes),
        [](const auto& elementArray) { return elementArray.Clone(); });
  }

  IssuerSources issuerSources;
  Result rv = certVerifier.VerifySSLServerCert(
      certBytes, time, aPinArg, aHostName, builtCertChain, certVerifierFlags,
      Some(std::move(peerCertsBytes)), stapledOCSPResponse,
      sctsFromTLSExtension, dcInfo, aOriginAttributes, &evStatus,
      &ocspStaplingStatus, &keySizeStatus, &pinningTelemetryInfo,
      &certificateTransparencyInfo, &aIsBuiltCertChainRootBuiltInRoot,
      &aMadeOCSPRequests, &issuerSources);

  CollectCertTelemetry(rv, evStatus, ocspStaplingStatus, keySizeStatus,
                       pinningTelemetryInfo, builtCertChain,
                       certificateTransparencyInfo, issuerSources);

  return rv;
}

PRErrorCode AuthCertificateParseResults(
    uint64_t aPtrForLog, const nsACString& aHostName, int32_t aPort,
    const OriginAttributes& aOriginAttributes,
    const nsCOMPtr<nsIX509Cert>& aCert, mozilla::pkix::Time aTime,
    PRErrorCode aCertVerificationError,
    /* out */
    nsITransportSecurityInfo::OverridableErrorCategory&
        aOverridableErrorCategory) {
  uint32_t probeValue = MapCertErrorToProbeValue(aCertVerificationError);
  glean::ssl::cert_verification_errors.AccumulateSingleSample(probeValue);

  Maybe<nsITransportSecurityInfo::OverridableErrorCategory>
      maybeOverridableErrorCategory =
          CategorizeCertificateError(aCertVerificationError);
  // If this isn't an overridable error, return it now. This will stop the
  // connection and report the given error.
  if (!maybeOverridableErrorCategory.isSome()) {
    return aCertVerificationError;
  }
  aOverridableErrorCategory = *maybeOverridableErrorCategory;

  bool overrideAllowed = false;
  nsresult rv = OverrideAllowedForHost(aPtrForLog, aHostName, aOriginAttributes,
                                       overrideAllowed);
  if (NS_FAILED(rv)) {
    return aCertVerificationError;
  }

  if (!overrideAllowed) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[0x%" PRIx64 "] HSTS or pinned host - no overrides allowed",
             aPtrForLog));
    return aCertVerificationError;
  }

  nsCOMPtr<nsICertOverrideService> overrideService =
      do_GetService(NS_CERTOVERRIDE_CONTRACTID);
  if (!overrideService) {
    return aCertVerificationError;
  }
  bool haveOverride;
  bool isTemporaryOverride;
  rv = overrideService->HasMatchingOverride(aHostName, aPort, aOriginAttributes,
                                            aCert, &isTemporaryOverride,
                                            &haveOverride);
  if (NS_FAILED(rv)) {
    return aCertVerificationError;
  }
  (void)isTemporaryOverride;
  if (haveOverride) {
    uint32_t probeValue =
        MapOverridableErrorToProbeValue(aCertVerificationError);
    glean::ssl::cert_error_overrides.AccumulateSingleSample(probeValue);
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[0x%" PRIx64 "] certificate error overridden", aPtrForLog));
    return 0;
  }

  return aCertVerificationError;
}

static nsTArray<nsTArray<uint8_t>> CreateCertBytesArray(
    const UniqueSECItemArray& aCertChain) {
  nsTArray<nsTArray<uint8_t>> certsBytes;
  for (size_t i = 0; i < aCertChain->len; i++) {
    nsTArray<uint8_t> certBytes;
    certBytes.AppendElements(aCertChain->items[i].data,
                             aCertChain->items[i].len);
    certsBytes.AppendElement(std::move(certBytes));
  }
  return certsBytes;
}

/*static*/
SECStatus SSLServerCertVerificationJob::Dispatch(
    uint64_t addrForLogging, void* aPinArg,
    nsTArray<nsTArray<uint8_t>>&& peerCertChain, const nsACString& aHostName,
    int32_t aPort, const OriginAttributes& aOriginAttributes,
    Maybe<nsTArray<uint8_t>>& stapledOCSPResponse,
    Maybe<nsTArray<uint8_t>>& sctsFromTLSExtension,
    Maybe<DelegatedCredentialInfo>& dcInfo, uint32_t providerFlags, Time time,
    uint32_t certVerifierFlags, SSLServerCertVerificationResult* aResultTask) {
  // Runs on the socket transport thread
  if (!aResultTask || peerCertChain.IsEmpty()) {
    MOZ_ASSERT_UNREACHABLE(
        "must have result task and non-empty peer cert chain");
    PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
    return SECFailure;
  }

  if (!gCertVerificationThreadPool) {
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    return SECFailure;
  }

  RefPtr<SSLServerCertVerificationJob> job(new SSLServerCertVerificationJob(
      addrForLogging, aPinArg, std::move(peerCertChain), aHostName, aPort,
      aOriginAttributes, stapledOCSPResponse, sctsFromTLSExtension, dcInfo,
      providerFlags, time, certVerifierFlags, aResultTask));

  nsresult nrv = gCertVerificationThreadPool->Dispatch(job, NS_DISPATCH_NORMAL);
  if (NS_FAILED(nrv)) {
    // We can't call SetCertVerificationResult here to change
    // mCertVerificationState because SetCertVerificationResult will call
    // libssl functions that acquire SSL locks that are already being held at
    // this point. However, we can set an error with PR_SetError and return
    // SECFailure, and the correct thing will happen (the error will be
    // propagated and this connection will be terminated).
    PRErrorCode error = nrv == NS_ERROR_OUT_OF_MEMORY ? PR_OUT_OF_MEMORY_ERROR
                                                      : PR_INVALID_STATE_ERROR;
    PR_SetError(error, 0);
    return SECFailure;
  }

  PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
  return SECWouldBlock;
}

NS_IMETHODIMP
SSLServerCertVerificationJob::Run() {
  // Runs on a cert verification thread and only on parent process.
  MOZ_ASSERT(XRE_IsParentProcess());

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%" PRIx64 "] SSLServerCertVerificationJob::Run", mAddrForLogging));

  RefPtr<SharedCertVerifier> certVerifier(GetDefaultCertVerifier());
  if (!certVerifier) {
    // We can't release this off the STS thread because some parts of it
    // are not threadsafe. Just leak mResultTask.
    mResultTask.forget().leak();
    return NS_ERROR_FAILURE;
  }

  TimeStamp jobStartTime = TimeStamp::Now();
  EVStatus evStatus;
  CertificateTransparencyInfo certificateTransparencyInfo;
  bool isCertChainRootBuiltInRoot = false;
  bool madeOCSPRequests = false;
  nsTArray<nsTArray<uint8_t>> builtChainBytesArray;
  nsTArray<uint8_t> certBytes(mPeerCertChain.ElementAt(0).Clone());
  Result result = AuthCertificate(
      *certVerifier, mPinArg, certBytes, mPeerCertChain, mHostName,
      mOriginAttributes, mStapledOCSPResponse, mSCTsFromTLSExtension, mDCInfo,
      mProviderFlags, mTime, mCertVerifierFlags, builtChainBytesArray, evStatus,
      certificateTransparencyInfo, isCertChainRootBuiltInRoot,
      madeOCSPRequests);

  TimeDuration elapsed = TimeStamp::Now() - jobStartTime;
  if (result == Success) {
    mozilla::glean::cert_verification_time::success.AccumulateRawDuration(
        elapsed);
    glean::ssl::cert_error_overrides.AccumulateSingleSample(1);

    nsresult rv = mResultTask->Dispatch(
        std::move(builtChainBytesArray), std::move(mPeerCertChain),
        TransportSecurityInfo::ConvertCertificateTransparencyInfoToStatus(
            certificateTransparencyInfo),
        evStatus, true, 0,
        nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET,
        isCertChainRootBuiltInRoot, mProviderFlags, madeOCSPRequests);
    if (NS_FAILED(rv)) {
      // We can't release this off the STS thread because some parts of it
      // are not threadsafe. Just leak mResultTask.
      mResultTask.forget().leak();
    }
    return rv;
  }

  mozilla::glean::cert_verification_time::failure.AccumulateRawDuration(
      elapsed);

  PRErrorCode error = MapResultToPRErrorCode(result);
  nsITransportSecurityInfo::OverridableErrorCategory overridableErrorCategory =
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET;
  nsCOMPtr<nsIX509Cert> cert(new nsNSSCertificate(std::move(certBytes)));
  PRErrorCode finalError = AuthCertificateParseResults(
      mAddrForLogging, mHostName, mPort, mOriginAttributes, cert, mTime, error,
      overridableErrorCategory);

  // NB: finalError may be 0 here, in which the connection will continue.
  nsresult rv = mResultTask->Dispatch(
      std::move(builtChainBytesArray), std::move(mPeerCertChain),
      TransportSecurityInfo::ConvertCertificateTransparencyInfoToStatus(
          certificateTransparencyInfo),
      EVStatus::NotEV, false, finalError, overridableErrorCategory,
      // If the certificate verifier returned Result::ERROR_BAD_CERT_DOMAIN,
      // a chain was built, so isCertChainRootBuiltInRoot is valid and
      // potentially useful. Otherwise, assume no chain was built.
      result == Result::ERROR_BAD_CERT_DOMAIN ? isCertChainRootBuiltInRoot
                                              : false,
      mProviderFlags, madeOCSPRequests);
  if (NS_FAILED(rv)) {
    // We can't release this off the STS thread because some parts of it
    // are not threadsafe. Just leak mResultTask.
    mResultTask.forget().leak();
  }
  return rv;
}

// Takes information needed for cert verification, does some consistency
//  checks and calls SSLServerCertVerificationJob::Dispatch.
SECStatus AuthCertificateHookInternal(
    CommonSocketControl* socketControl, const void* aPtrForLogging,
    const nsACString& hostName, nsTArray<nsTArray<uint8_t>>&& peerCertChain,
    Maybe<nsTArray<uint8_t>>& stapledOCSPResponse,
    Maybe<nsTArray<uint8_t>>& sctsFromTLSExtension,
    Maybe<DelegatedCredentialInfo>& dcInfo, uint32_t providerFlags,
    uint32_t certVerifierFlags) {
  // Runs on the socket transport thread

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p] starting AuthCertificateHookInternal\n", aPtrForLogging));

  if (!socketControl || peerCertChain.IsEmpty()) {
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    return SECFailure;
  }

  bool onSTSThread;
  nsresult nrv;
  nsCOMPtr<nsIEventTarget> sts =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &nrv);
  if (NS_SUCCEEDED(nrv)) {
    nrv = sts->IsOnCurrentThread(&onSTSThread);
  }

  if (NS_FAILED(nrv)) {
    NS_ERROR("Could not get STS service or IsOnCurrentThread failed");
    PR_SetError(PR_UNKNOWN_ERROR, 0);
    return SECFailure;
  }

  MOZ_ASSERT(onSTSThread);

  if (!onSTSThread) {
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    return SECFailure;
  }

  uint64_t addr = reinterpret_cast<uintptr_t>(aPtrForLogging);
  RefPtr resultTask =
      MakeRefPtr<SSLServerCertVerificationResult>(socketControl);

  // We *must* do certificate verification on a background thread because
  // we need the socket transport thread to be free for our OCSP requests,
  // and we *want* to do certificate verification on a background thread
  // because of the performance benefits of doing so.
  return SSLServerCertVerificationJob::Dispatch(
      addr, socketControl, std::move(peerCertChain), hostName,
      socketControl->GetPort(), socketControl->GetOriginAttributes(),
      stapledOCSPResponse, sctsFromTLSExtension, dcInfo, providerFlags, Now(),
      certVerifierFlags, resultTask);
}

// Extracts whatever information we need out of fd (using SSL_*) and passes it
// to AuthCertificateHookInternal. AuthCertificateHookInternal will call
// SSLServerCertVerificationJob::Dispatch. SSLServerCertVerificationJob
// should never do anything with fd except logging.
SECStatus AuthCertificateHook(void* arg, PRFileDesc* fd, PRBool checkSig,
                              PRBool isServer) {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p] starting AuthCertificateHook\n", fd));

  // Modern libssl always passes PR_TRUE for checkSig, and we have no means of
  // doing verification without checking signatures.
  MOZ_ASSERT(checkSig, "AuthCertificateHook: checkSig unexpectedly false");

  // PSM never causes libssl to call this function with PR_TRUE for isServer,
  // and many things in PSM assume that we are a client.
  MOZ_ASSERT(!isServer, "AuthCertificateHook: isServer unexpectedly true");

  NSSSocketControl* socketInfo = static_cast<NSSSocketControl*>(arg);

  UniqueSECItemArray peerCertChain;
  SECStatus rv =
      SSL_PeerCertificateChainDER(fd, TempPtrToSetter(&peerCertChain));

  if (!checkSig || isServer || !socketInfo || rv != SECSuccess ||
      !peerCertChain || peerCertChain->len == 0) {
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    return SECFailure;
  }
  socketInfo->SetFullHandshake();

  nsTArray<nsTArray<uint8_t>> peerCertsBytes =
      CreateCertBytesArray(peerCertChain);

  if (BlockServerCertChangeForSpdy(socketInfo, peerCertsBytes[0]) !=
      SECSuccess) {
    return SECFailure;
  }

  // SSL_PeerStapledOCSPResponses will never return a non-empty response if
  // OCSP stapling wasn't enabled because libssl wouldn't have let the server
  // return a stapled OCSP response.
  // We don't own these pointers.
  const SECItemArray* csa = SSL_PeerStapledOCSPResponses(fd);
  Maybe<nsTArray<uint8_t>> stapledOCSPResponse;
  // we currently only support single stapled responses
  if (csa && csa->len == 1) {
    stapledOCSPResponse.emplace();
    stapledOCSPResponse->SetCapacity(csa->items[0].len);
    stapledOCSPResponse->AppendElements(csa->items[0].data, csa->items[0].len);
  }

  Maybe<nsTArray<uint8_t>> sctsFromTLSExtension;
  const SECItem* sctsFromTLSExtensionSECItem = SSL_PeerSignedCertTimestamps(fd);
  if (sctsFromTLSExtensionSECItem) {
    sctsFromTLSExtension.emplace();
    sctsFromTLSExtension->SetCapacity(sctsFromTLSExtensionSECItem->len);
    sctsFromTLSExtension->AppendElements(sctsFromTLSExtensionSECItem->data,
                                         sctsFromTLSExtensionSECItem->len);
  }

  uint32_t providerFlags = 0;
  socketInfo->GetProviderFlags(&providerFlags);

  uint32_t certVerifierFlags = 0;
  if (!StaticPrefs::security_ssl_enable_ocsp_stapling() ||
      !StaticPrefs::security_ssl_enable_ocsp_must_staple()) {
    certVerifierFlags |= CertVerifier::FLAG_TLS_IGNORE_STATUS_REQUEST;
  }

  // Get DC information
  Maybe<DelegatedCredentialInfo> dcInfo;
  SSLPreliminaryChannelInfo channelPreInfo;
  rv = SSL_GetPreliminaryChannelInfo(fd, &channelPreInfo,
                                     sizeof(channelPreInfo));
  if (rv != SECSuccess) {
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    return SECFailure;
  }
  if (channelPreInfo.peerDelegCred) {
    dcInfo.emplace(DelegatedCredentialInfo(channelPreInfo.signatureScheme,
                                           channelPreInfo.authKeyBits));
  }

  // If we configured an ECHConfig and NSS returned the public name
  // for verification, ECH was rejected. Proceed, verifying to the
  // public name. The result determines how NSS will fail (i.e. with
  // any provided retry_configs if successful). See draft-ietf-tls-esni-08.
  nsCString echConfig;
  nsresult nsrv = socketInfo->GetEchConfig(echConfig);
  bool verifyToEchPublicName =
      NS_SUCCEEDED(nsrv) && echConfig.Length() && channelPreInfo.echPublicName;

  const nsCString echPublicName(channelPreInfo.echPublicName);
  const nsACString& hostname =
      verifyToEchPublicName ? echPublicName : socketInfo->GetHostName();
  socketInfo->SetCertVerificationWaiting();
  rv = AuthCertificateHookInternal(socketInfo, static_cast<const void*>(fd),
                                   hostname, std::move(peerCertsBytes),
                                   stapledOCSPResponse, sctsFromTLSExtension,
                                   dcInfo, providerFlags, certVerifierFlags);
  return rv;
}

// Takes information needed for cert verification, does some consistency
// checks and calls SSLServerCertVerificationJob::Dispatch.
// This function is used for Quic.
SECStatus AuthCertificateHookWithInfo(
    CommonSocketControl* socketControl, const nsACString& aHostName,
    const void* aPtrForLogging, nsTArray<nsTArray<uint8_t>>&& peerCertChain,
    Maybe<nsTArray<nsTArray<uint8_t>>>& stapledOCSPResponses,
    Maybe<nsTArray<uint8_t>>& sctsFromTLSExtension, uint32_t providerFlags) {
  if (peerCertChain.IsEmpty()) {
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    return SECFailure;
  }

  // we currently only support single stapled responses
  Maybe<nsTArray<uint8_t>> stapledOCSPResponse;
  if (stapledOCSPResponses && (stapledOCSPResponses->Length() == 1)) {
    stapledOCSPResponse.emplace(stapledOCSPResponses->ElementAt(0).Clone());
  }

  uint32_t certVerifierFlags = 0;
  if (!StaticPrefs::security_ssl_enable_ocsp_stapling() ||
      !StaticPrefs::security_ssl_enable_ocsp_must_staple()) {
    certVerifierFlags |= CertVerifier::FLAG_TLS_IGNORE_STATUS_REQUEST;
  }

  // Need to update Quic stack to reflect the PreliminaryInfo fields
  // for Delegated Credentials.
  Maybe<DelegatedCredentialInfo> dcInfo;

  return AuthCertificateHookInternal(socketControl, aPtrForLogging, aHostName,
                                     std::move(peerCertChain),
                                     stapledOCSPResponse, sctsFromTLSExtension,
                                     dcInfo, providerFlags, certVerifierFlags);
}

NS_IMPL_ISUPPORTS_INHERITED0(SSLServerCertVerificationResult, Runnable)

SSLServerCertVerificationResult::SSLServerCertVerificationResult(
    CommonSocketControl* socketControl)
    : Runnable("psm::SSLServerCertVerificationResult"),
      mSocketControl(socketControl),
      mCertificateTransparencyStatus(0),
      mEVStatus(EVStatus::NotEV),
      mSucceeded(false),
      mFinalError(0),
      mOverridableErrorCategory(
          nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET),
      mIsBuiltCertChainRootBuiltInRoot(false),
      mProviderFlags(0),
      mMadeOCSPRequests(false) {}

nsresult SSLServerCertVerificationResult::Dispatch(
    nsTArray<nsTArray<uint8_t>>&& aBuiltChain,
    nsTArray<nsTArray<uint8_t>>&& aPeerCertChain,
    uint16_t aCertificateTransparencyStatus, EVStatus aEVStatus,
    bool aSucceeded, PRErrorCode aFinalError,
    nsITransportSecurityInfo::OverridableErrorCategory
        aOverridableErrorCategory,
    bool aIsBuiltCertChainRootBuiltInRoot, uint32_t aProviderFlags,
    bool aMadeOCSPRequests) {
  mBuiltChain = std::move(aBuiltChain);
  mPeerCertChain = std::move(aPeerCertChain);
  mCertificateTransparencyStatus = aCertificateTransparencyStatus;
  mEVStatus = aEVStatus;
  mSucceeded = aSucceeded;
  mFinalError = aFinalError;
  mOverridableErrorCategory = aOverridableErrorCategory;
  mIsBuiltCertChainRootBuiltInRoot = aIsBuiltCertChainRootBuiltInRoot;
  mProviderFlags = aProviderFlags;
  mMadeOCSPRequests = aMadeOCSPRequests;

  if (mSucceeded &&
      (mBuiltChain.IsEmpty() || mFinalError != 0 ||
       mOverridableErrorCategory !=
           nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET)) {
    MOZ_ASSERT_UNREACHABLE(
        "if certificate verification succeeded without overridden errors, the "
        "built chain shouldn't be empty and any error bits should be unset");
    mSucceeded = false;
    mFinalError = SEC_ERROR_LIBRARY_FAILURE;
  }
  // Note that mSucceeded can be false while mFinalError is 0, in which case
  // the connection will proceed.
  if (!mSucceeded && mPeerCertChain.IsEmpty()) {
    MOZ_ASSERT_UNREACHABLE(
        "if certificate verification failed, the peer chain shouldn't be "
        "empty");
    mFinalError = SEC_ERROR_LIBRARY_FAILURE;
  }

  nsresult rv;
  nsCOMPtr<nsIEventTarget> stsTarget =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  MOZ_ASSERT(stsTarget, "Failed to get socket transport service event target");
  if (!stsTarget) {
    // This has to be released on STS; just leak it
    mSocketControl.forget().leak();
    return NS_ERROR_FAILURE;
  }
  rv = stsTarget->Dispatch(this, NS_DISPATCH_NORMAL);
  MOZ_ASSERT(NS_SUCCEEDED(rv),
             "Failed to dispatch SSLServerCertVerificationResult");
  return rv;
}

NS_IMETHODIMP
SSLServerCertVerificationResult::Run() {
#ifdef DEBUG
  bool onSTSThread = false;
  nsresult nrv;
  nsCOMPtr<nsIEventTarget> sts =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &nrv);
  if (NS_SUCCEEDED(nrv)) {
    nrv = sts->IsOnCurrentThread(&onSTSThread);
  }

  MOZ_ASSERT(onSTSThread);
#endif

  mSocketControl->SetMadeOCSPRequests(mMadeOCSPRequests);
  mSocketControl->SetIsBuiltCertChainRootBuiltInRoot(
      mIsBuiltCertChainRootBuiltInRoot);
  mSocketControl->SetCertificateTransparencyStatus(
      mCertificateTransparencyStatus);

  if (mSucceeded) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("SSLServerCertVerificationResult::Run setting NEW cert"));
    nsTArray<uint8_t> certBytes(mBuiltChain.ElementAt(0).Clone());
    nsCOMPtr<nsIX509Cert> cert(new nsNSSCertificate(std::move(certBytes)));
    mSocketControl->SetServerCert(cert, mEVStatus);
    mSocketControl->SetSucceededCertChain(std::move(mBuiltChain));
  } else {
    nsTArray<uint8_t> certBytes(mPeerCertChain.ElementAt(0).Clone());
    nsCOMPtr<nsIX509Cert> cert(new nsNSSCertificate(std::move(certBytes)));
    mSocketControl->SetServerCert(cert, EVStatus::NotEV);
    if (mOverridableErrorCategory !=
        nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
      mSocketControl->SetStatusErrorBits(mOverridableErrorCategory);
    }
  }

  mSocketControl->SetHandshakeCertificates(std::move(mPeerCertChain));
  mSocketControl->SetCertVerificationResult(mFinalError);
  // Release this reference to the socket control so that it will be freed on
  // the socket thread.
  mSocketControl = nullptr;
  return NS_OK;
}

}  // namespace psm
}  // namespace mozilla
