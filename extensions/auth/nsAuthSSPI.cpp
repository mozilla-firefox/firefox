/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Negotiate Authentication Support Module
//
// Described by IETF Internet draft: draft-brezak-kerberos-http-00.txt
// (formerly draft-brezak-spnego-http-04.txt)
//
// Also described here:
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dnsecure/html/http-sso-1.asp
//

#include "nsAuthSSPI.h"
#include "nsComponentManagerUtils.h"
#include "nsICancelable.h"
#include "nsIDNSService.h"
#include "nsIDNSRecord.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsCOMPtr.h"
#include "nsICryptoHash.h"
#include "mozilla/Atomics.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"
#include "mozilla/StaticPrefs_network.h"

#include <windows.h>

// for safer certificate parsing
#include "nss/mozpkix/pkixutil.h"
#include "nss/mozpkix/pkixder.h"

#define SEC_SUCCESS(Status) ((Status) >= 0)

#ifndef KERB_WRAP_NO_ENCRYPT
#  define KERB_WRAP_NO_ENCRYPT 0x80000001
#endif

#ifndef SECBUFFER_PADDING
#  define SECBUFFER_PADDING 9
#endif

#ifndef SECBUFFER_STREAM
#  define SECBUFFER_STREAM 10
#endif

//-----------------------------------------------------------------------------

static const wchar_t* const pTypeName[] = {L"Kerberos", L"Negotiate", L"NTLM"};

#ifdef DEBUG
#  define CASE_(_x) \
    case _x:        \
      return #_x;
static const char* MapErrorCode(int rc) {
  switch (rc) {
    CASE_(SEC_E_OK)
    CASE_(SEC_I_CONTINUE_NEEDED)
    CASE_(SEC_I_COMPLETE_NEEDED)
    CASE_(SEC_I_COMPLETE_AND_CONTINUE)
    CASE_(SEC_E_INCOMPLETE_MESSAGE)
    CASE_(SEC_I_INCOMPLETE_CREDENTIALS)
    CASE_(SEC_E_INVALID_HANDLE)
    CASE_(SEC_E_TARGET_UNKNOWN)
    CASE_(SEC_E_LOGON_DENIED)
    CASE_(SEC_E_INTERNAL_ERROR)
    CASE_(SEC_E_NO_CREDENTIALS)
    CASE_(SEC_E_NO_AUTHENTICATING_AUTHORITY)
    CASE_(SEC_E_INSUFFICIENT_MEMORY)
    CASE_(SEC_E_INVALID_TOKEN)
  }
  return "<unknown>";
}
#else
#  define MapErrorCode(_rc) ""
#endif

//-----------------------------------------------------------------------------

static PSecurityFunctionTableW sspi;

static nsresult InitSSPI() {
  LOG(("  InitSSPI\n"));

  sspi = InitSecurityInterfaceW();
  if (!sspi) {
    LOG(("InitSecurityInterfaceW failed"));
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

nsAuthSSPI::nsAuthSSPI(pType package)
    : mServiceFlags(REQ_DEFAULT),
      mMaxTokenLen(0),
      mPackage(package),
      mCertDERData(nullptr),
      mCertDERLength(0) {
  memset(&mCred, 0, sizeof(mCred));
  memset(&mCtxt, 0, sizeof(mCtxt));
}

nsAuthSSPI::~nsAuthSSPI() {
  Reset();

  if (mCred.dwLower || mCred.dwUpper) {
    (sspi->FreeCredentialsHandle)(&mCred);
    memset(&mCred, 0, sizeof(mCred));
  }
}

void nsAuthSSPI::Reset() {
  mIsFirst = true;

  if (mCertDERData) {
    free(mCertDERData);
    mCertDERData = nullptr;
    mCertDERLength = 0;
  }

  if (mCtxt.dwLower || mCtxt.dwUpper) {
    (sspi->DeleteSecurityContext)(&mCtxt);
    memset(&mCtxt, 0, sizeof(mCtxt));
  }
}

NS_IMPL_ISUPPORTS(nsAuthSSPI, nsIAuthModule, nsIDNSListener)

nsresult nsAuthSSPI::CompleteInit() {
  nsresult rv;

  if (!sspi) {
    rv = InitSSPI();
    if (NS_FAILED(rv)) return rv;
  }

  SEC_WCHAR* package = (SEC_WCHAR*)pTypeName[(int)mPackage];

  SECURITY_STATUS rc;
  PSecPkgInfoW pinfo;
  rc = (sspi->QuerySecurityPackageInfoW)(package, &pinfo);
  if (rc != SEC_E_OK) {
    LOG(("%S package not found\n", package));
    return NS_ERROR_UNEXPECTED;
  }
  mMaxTokenLen = pinfo->cbMaxToken;
  (sspi->FreeContextBuffer)(pinfo);

  MS_TimeStamp useBefore;

  SEC_WINNT_AUTH_IDENTITY_W ai;
  SEC_WINNT_AUTH_IDENTITY_W* pai = nullptr;

  // domain, username, and password will be null if nsHttpNTLMAuth's
  // ChallengeReceived returns false for identityInvalid. Use default
  // credentials in this case by passing null for pai.
  if (!mUsername.IsEmpty() && !mPassword.IsEmpty()) {
    ai.Domain = reinterpret_cast<unsigned short*>(mDomain.BeginWriting());
    ai.DomainLength = mDomain.Length();
    ai.User = reinterpret_cast<unsigned short*>(mUsername.BeginWriting());
    ai.UserLength = mUsername.Length();
    ai.Password = reinterpret_cast<unsigned short*>(mPassword.BeginWriting());
    ai.PasswordLength = mPassword.Length();
    ai.Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;
    pai = &ai;
  }

  rc = (sspi->AcquireCredentialsHandleW)(nullptr, package, SECPKG_CRED_OUTBOUND,
                                         nullptr, pai, nullptr, nullptr, &mCred,
                                         &useBefore);
  if (rc != SEC_E_OK) return NS_ERROR_UNEXPECTED;

  // CompleteInit runs on background threads, so several auths may reach this
  // concurrently.
  static mozilla::Atomic<bool> sTelemetrySent{false};
  if (sTelemetrySent.compareExchange(false, true)) {
    mozilla::glean::security::ntlm_module_used.AccumulateSingleSample(
        mServiceFlags & nsIAuthModule::REQ_PROXY_AUTH
            ? NTLM_MODULE_WIN_API_PROXY
            : NTLM_MODULE_WIN_API_DIRECT);
  }

  LOG(("AcquireCredentialsHandle() succeeded.\n"));
  return NS_OK;
}

NS_IMETHODIMP
nsAuthSSPI::Init(const nsACString& aServiceName, uint32_t aServiceFlags,
                 const nsAString& aDomain, const nsAString& aUsername,
                 const nsAString& aPassword) {
  LOG(("  nsAuthSSPI::Init\n"));

  mIsFirst = true;
  free(mCertDERData);
  mCertDERLength = 0;
  mCertDERData = nullptr;

  // The caller must supply a service name to be used. (For why we now require
  // a service name for NTLM, see bug 487872.)
  NS_ENSURE_TRUE(!aServiceName.IsEmpty(), NS_ERROR_INVALID_ARG);

  // The incoming serviceName is in the format: "protocol@hostname",
  // SSPI expects "<service class>/<hostname>", so swap the '@' for a '/'.
  mServiceName = aServiceName;
  int32_t index = mServiceName.FindChar('@');
  if (index == kNotFound) return NS_ERROR_UNEXPECTED;
  mServiceName.Replace(index, 1, '/');

  mServiceFlags = aServiceFlags;
  // Keep a copy of these strings for the duration
  mDomain = aDomain;
  mUsername = aUsername;
  mPassword = aPassword;

  return CompleteInit();
}

NS_IMETHODIMP
nsAuthSSPI::InitAsync(const nsACString& aServiceName, uint32_t aServiceFlags,
                      const nsAString& aDomain, const nsAString& aUsername,
                      const nsAString& aPassword,
                      nsIAuthModuleInitCallback* aCallback) {
  LOG(("  nsAuthSSPI::InitAsync\n"));
  NS_ENSURE_ARG(aCallback);

  mIsFirst = true;
  free(mCertDERData);
  mCertDERLength = 0;
  mCertDERData = nullptr;

  // The caller must supply a service name to be used. (For why we now require
  // a service name for NTLM, see bug 487872.)
  NS_ENSURE_TRUE(!aServiceName.IsEmpty(), NS_ERROR_INVALID_ARG);

  mServiceFlags = aServiceFlags;
  // Keep a copy of these strings for the duration
  mDomain = aDomain;
  mUsername = aUsername;
  mPassword = aPassword;

  nsAutoCString buf(aServiceName);
  int32_t index = buf.FindChar('@');
  if (index == kNotFound) return NS_ERROR_UNEXPECTED;

  if (mPackage == PACKAGE_TYPE_NTLM) {
    // (bug 535193) For NTLM, just use the uri host, do not do canonical host
    // lookups. The incoming serviceName is in the format:
    // "protocol@hostname", SSPI expects "<service class>/<hostname>", so swap
    // the '@' for a '/'.
    mServiceName = buf;
    mServiceName.Replace(index, 1, '/');
    nsresult rv = CompleteInit();
    aCallback->OnInitDone(rv);
    return NS_OK;
  }

  // Kerberos requires the canonical host. Resolve it asynchronously.
  mPendingServicePrefix = StringHead(buf, index);
  mInitCallback = aCallback;

  nsresult rv;
  nsCOMPtr<nsIDNSService> dnsService =
      do_GetService(NS_DNSSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    mInitCallback = nullptr;
    return rv;
  }

  // Resolve on a background task queue rather than passing a null target,
  // which would run OnLookupComplete on the DNS thread. CompleteInit() and
  // the credential generation the callback goes on to do may both block, so
  // they must not hold up name resolution.
  nsCOMPtr<nsISerialEventTarget> target;
  rv = NS_CreateBackgroundTaskQueue("nsAuthSSPI", getter_AddRefs(target));
  if (NS_FAILED(rv)) {
    mInitCallback = nullptr;
    return rv;
  }

  nsCOMPtr<nsICancelable> cancelable;
  mozilla::OriginAttributes attrs;
  rv = dnsService->AsyncResolveNative(
      Substring(buf, index + 1), nsIDNSService::RESOLVE_TYPE_DEFAULT,
      nsIDNSService::RESOLVE_CANONICAL_NAME, nullptr, this, target, attrs,
      getter_AddRefs(cancelable));
  if (NS_FAILED(rv)) {
    mInitCallback = nullptr;
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAuthSSPI::OnLookupComplete(nsICancelable* aRequest, nsIDNSRecord* aRecord,
                             nsresult aStatus) {
  MOZ_ASSERT(!NS_IsMainThread());

  nsCOMPtr<nsIAuthModuleInitCallback> callback = std::move(mInitCallback);
  MOZ_ASSERT(callback);

  if (NS_FAILED(aStatus)) {
    callback->OnInitDone(aStatus);
    return NS_OK;
  }

  nsCOMPtr<nsIDNSAddrRecord> rec = do_QueryInterface(aRecord);
  if (!rec) {
    callback->OnInitDone(NS_ERROR_UNEXPECTED);
    return NS_OK;
  }

  nsAutoCString cname;
  nsresult rv = rec->GetCanonicalName(cname);
  if (NS_FAILED(rv)) {
    callback->OnInitDone(rv);
    return NS_OK;
  }

  mServiceName = mPendingServicePrefix + "/"_ns + cname;
  LOG(("Using SPN of [%s]\n", mServiceName.get()));

  rv = CompleteInit();
  callback->OnInitDone(rv);
  return NS_OK;
}

nsresult nsAuthSSPI::MakeChannelBindings(mozilla::UniqueFreePtr<char>& aBuffer,
                                         uint32_t& aBufferLength) {
  // String for end-point bindings.
  const char end_point[] = "tls-server-end-point:";
  const uint32_t end_point_length = sizeof(end_point) - 1;

  // Default to SHA256 for compatibility, but detect SHA384 and SHA512
  uint32_t hashAlgorithm = nsICryptoHash::SHA256;
  uint32_t hashSize = 32;  // SHA256 hash size

  // Compute the hash size.
  [&]() {
    if (!mozilla::StaticPrefs::network_auth_sspi_detect_hash()) {
      // This check only exists to make sure that the hash algorithm check
      // doesn't break previous working behaviour.
      return;
    }
    using namespace mozilla::pkix;
    Input certDER;

    mozilla::pkix::Result pkixResult =
        certDER.Init(static_cast<const uint8_t*>(mCertDERData), mCertDERLength);
    if (pkixResult != Success) {
      return;
    }

    BackCert cert(certDER, EndEntityOrCA::MustBeEndEntity, nullptr);
    pkixResult = cert.Init();
    if (pkixResult != Success) {
      return;
    }

    // Parse the signature algorithm from the signed data
    der::PublicKeyAlgorithm publicKeyAlg;
    std::optional<DigestAlgorithm> digestAlg;
    Reader signatureAlgorithmReader(cert.GetSignedData().algorithm);
    pkixResult = der::SignatureAlgorithmIdentifierValue(
        signatureAlgorithmReader, publicKeyAlg, digestAlg);

    if (pkixResult != Success || !digestAlg) {
      return;
    }
    // Map digest algorithms to hash algorithms for Extended Protection
    switch (*digestAlg) {
      case DigestAlgorithm::sha384:
        hashAlgorithm = nsICryptoHash::SHA384;
        hashSize = 48;  // SHA384 hash size
        break;
      case DigestAlgorithm::sha512:
        hashAlgorithm = nsICryptoHash::SHA512;
        hashSize = 64;  // SHA512 hash size
        break;
      case DigestAlgorithm::sha256:
      default:
        // Use SHA256 as default for compatibility
        hashAlgorithm = nsICryptoHash::SHA256;
        hashSize = 32;
        break;
    }
  }();

  // Create Endpoint Binding structure with correct size
  SEC_CHANNEL_BINDINGS endpointBinding;
  endpointBinding.dwInitiatorAddrType = 0;
  endpointBinding.cbInitiatorLength = 0;
  endpointBinding.dwInitiatorOffset = 0;
  endpointBinding.dwAcceptorAddrType = 0;
  endpointBinding.cbAcceptorLength = 0;
  endpointBinding.dwAcceptorOffset = 0;
  endpointBinding.cbApplicationDataLength = hashSize + end_point_length;
  endpointBinding.dwApplicationDataOffset = sizeof(SEC_CHANNEL_BINDINGS);

  aBufferLength = endpointBinding.cbApplicationDataLength +
                  endpointBinding.dwApplicationDataOffset;
  aBuffer.reset(static_cast<char*>(moz_xmalloc(aBufferLength)));

  // Helper to write in the memory block that stores the CBT
  char* sspi_cbt_ptr = aBuffer.get();

  memcpy(sspi_cbt_ptr, &endpointBinding,
         endpointBinding.dwApplicationDataOffset);
  sspi_cbt_ptr += endpointBinding.dwApplicationDataOffset;

  memcpy(sspi_cbt_ptr, end_point, end_point_length);
  sspi_cbt_ptr += end_point_length;

  nsAutoCString hashString;
  nsresult rv = NS_ERROR_FAILURE;

  nsCOMPtr<nsICryptoHash> crypto;
  crypto = do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = crypto->Init(hashAlgorithm);
  }
  if (NS_SUCCEEDED(rv)) {
    rv = crypto->Update((unsigned char*)mCertDERData, mCertDERLength);
  }
  if (NS_SUCCEEDED(rv)) rv = crypto->Finish(false, hashString);

  if (NS_FAILED(rv)) {
    return rv;
  }

  // Store the computed hash in memory right after the Endpoint
  // structure and the "tls-server-end-point:" char array
  memcpy(sspi_cbt_ptr, hashString.get(), hashSize);

  return NS_OK;
}

// The arguments inToken and inTokenLen are used to pass in the server
// certificate (when available) in the first call of the function. The
// second time these arguments hold an input token.
NS_IMETHODIMP
nsAuthSSPI::GetNextToken(const void* inToken, uint32_t inTokenLen,
                         void** outToken, uint32_t* outTokenLen) {
  SECURITY_STATUS rc;
  MS_TimeStamp ignored;

  DWORD ctxAttr, ctxReq = 0;
  CtxtHandle* ctxIn = nullptr;
  SecBufferDesc ibd, obd;
  // Optional second input buffer for the CBT (Channel Binding Token)
  SecBuffer ib[2], ob;
  // Block of memory that stores the CBT
  mozilla::UniqueFreePtr<char> sspi_cbt;

  LOG(("entering nsAuthSSPI::GetNextToken()\n"));

  if (!mCred.dwLower && !mCred.dwUpper) {
    LOG(("nsAuthSSPI::GetNextToken(), not initialized. exiting."));
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (mServiceFlags & REQ_DELEGATE) ctxReq |= ISC_REQ_DELEGATE;
  if (mServiceFlags & REQ_MUTUAL_AUTH) ctxReq |= ISC_REQ_MUTUAL_AUTH;

  ibd.ulVersion = SECBUFFER_VERSION;
  ibd.cBuffers = 0;
  ibd.pBuffers = ib;

  if (inToken) {
    if (mIsFirst) {
      // First time if it comes with a token,
      // the token represents the server certificate.
      mIsFirst = false;
      mCertDERLength = inTokenLen;
      mCertDERData = moz_xmalloc(inTokenLen);
      memcpy(mCertDERData, inToken, inTokenLen);

      // We are starting a new authentication sequence.
      // If we have already initialized our
      // security context, then we're in trouble because it means that the
      // first sequence failed.  We need to bail or else we might end up in
      // an infinite loop.
      if (mCtxt.dwLower || mCtxt.dwUpper) {
        LOG(("Cannot restart authentication sequence!"));
        return NS_ERROR_UNEXPECTED;
      }
      // The certificate needs to be erased before being passed
      // to InitializeSecurityContextW().
      inToken = nullptr;
      inTokenLen = 0;
    } else {
      ctxIn = &mCtxt;
    }
  } else {  // First time and without a token (no server certificate)
    // We are starting a new authentication sequence.  If we have already
    // initialized our security context, then we're in trouble because it
    // means that the first sequence failed.  We need to bail or else we
    // might end up in an infinite loop.
    if (mCtxt.dwLower || mCtxt.dwUpper || mCertDERData || mCertDERLength) {
      LOG(("Cannot restart authentication sequence!"));
      return NS_ERROR_UNEXPECTED;
    }
    mIsFirst = false;
  }

  // If we have stored a certificate, the Channel Binding Token needs to be
  // generated and sent in the first input buffer. Kerberos carries the AP-REQ
  // in the very first token, so for SPNEGO the bindings have to be attached
  // from the initial call on. Raw NTLM instead carries them in message 3,
  // generated on a continuation call, so that package keeps sending them only
  // once a context exists.
  bool haveContext = mCtxt.dwLower || mCtxt.dwUpper;
  if (mCertDERLength > 0 && (haveContext || mPackage != PACKAGE_TYPE_NTLM)) {
    uint32_t cbtLength = 0;
    nsresult rv = MakeChannelBindings(sspi_cbt, cbtLength);
    if (NS_FAILED(rv)) {
      return rv;
    }

    ib[ibd.cBuffers].BufferType = SECBUFFER_CHANNEL_BINDINGS;
    ib[ibd.cBuffers].cbBuffer = cbtLength;
    ib[ibd.cBuffers].pvBuffer = sspi_cbt.get();
    ibd.cBuffers++;
  }

  if (inToken) {
    ib[ibd.cBuffers].BufferType = SECBUFFER_TOKEN;
    ib[ibd.cBuffers].cbBuffer = inTokenLen;
    ib[ibd.cBuffers].pvBuffer = (void*)inToken;
    ibd.cBuffers++;
  }

  obd.ulVersion = SECBUFFER_VERSION;
  obd.cBuffers = 1;
  obd.pBuffers = &ob;
  ob.BufferType = SECBUFFER_TOKEN;
  ob.cbBuffer = mMaxTokenLen;
  ob.pvBuffer = moz_xmalloc(ob.cbBuffer);
  memset(ob.pvBuffer, 0, ob.cbBuffer);

  NS_ConvertUTF8toUTF16 wSN(mServiceName);
  SEC_WCHAR* sn = (SEC_WCHAR*)wSN.get();

  rc = (sspi->InitializeSecurityContextW)(
      &mCred, ctxIn, sn, ctxReq, 0, SECURITY_NATIVE_DREP,
      ibd.cBuffers ? &ibd : nullptr, 0, &mCtxt, &obd, &ctxAttr, &ignored);
  if (rc == SEC_I_CONTINUE_NEEDED || rc == SEC_E_OK) {
    if (rc == SEC_E_OK)
      LOG(("InitializeSecurityContext: succeeded.\n"));
    else
      LOG(("InitializeSecurityContext: continue.\n"));

    if (!ob.cbBuffer) {
      free(ob.pvBuffer);
      ob.pvBuffer = nullptr;
    }
    *outToken = ob.pvBuffer;
    *outTokenLen = ob.cbBuffer;

    if (rc == SEC_E_OK) return NS_SUCCESS_AUTH_FINISHED;

    return NS_OK;
  }

  LOG(("InitializeSecurityContext failed [rc=%ld:%s]\n", rc, MapErrorCode(rc)));
  Reset();
  free(ob.pvBuffer);
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsAuthSSPI::Unwrap(const void* inToken, uint32_t inTokenLen, void** outToken,
                   uint32_t* outTokenLen) {
  SECURITY_STATUS rc;
  SecBufferDesc ibd;
  SecBuffer ib[2];

  ibd.cBuffers = 2;
  ibd.pBuffers = ib;
  ibd.ulVersion = SECBUFFER_VERSION;

  // SSPI Buf
  ib[0].BufferType = SECBUFFER_STREAM;
  ib[0].cbBuffer = inTokenLen;
  ib[0].pvBuffer = moz_xmalloc(ib[0].cbBuffer);

  memcpy(ib[0].pvBuffer, inToken, inTokenLen);

  // app data
  ib[1].BufferType = SECBUFFER_DATA;
  ib[1].cbBuffer = 0;
  ib[1].pvBuffer = nullptr;

  rc = (sspi->DecryptMessage)(&mCtxt, &ibd,
                              0,  // no sequence numbers
                              nullptr);

  if (SEC_SUCCESS(rc)) {
    // check if ib[1].pvBuffer is really just ib[0].pvBuffer, in which
    // case we can let the caller free it. Otherwise, we need to
    // clone it, and free the original
    if (ib[0].pvBuffer == ib[1].pvBuffer) {
      *outToken = ib[1].pvBuffer;
    } else {
      *outToken = moz_xmemdup(ib[1].pvBuffer, ib[1].cbBuffer);
      free(ib[0].pvBuffer);
    }
    *outTokenLen = ib[1].cbBuffer;
  } else
    free(ib[0].pvBuffer);

  if (!SEC_SUCCESS(rc)) return NS_ERROR_FAILURE;

  return NS_OK;
}

// utility class used to free memory on exit
class secBuffers {
 public:
  SecBuffer ib[3];

  secBuffers() { memset(&ib, 0, sizeof(ib)); }

  ~secBuffers() {
    if (ib[0].pvBuffer) free(ib[0].pvBuffer);

    if (ib[1].pvBuffer) free(ib[1].pvBuffer);

    if (ib[2].pvBuffer) free(ib[2].pvBuffer);
  }
};

NS_IMETHODIMP
nsAuthSSPI::Wrap(const void* inToken, uint32_t inTokenLen, bool confidential,
                 void** outToken, uint32_t* outTokenLen) {
  SECURITY_STATUS rc;

  SecBufferDesc ibd;
  secBuffers bufs;
  SecPkgContext_Sizes sizes;

  rc = (sspi->QueryContextAttributesW)(&mCtxt, SECPKG_ATTR_SIZES, &sizes);

  if (!SEC_SUCCESS(rc)) return NS_ERROR_FAILURE;

  ibd.cBuffers = 3;
  ibd.pBuffers = bufs.ib;
  ibd.ulVersion = SECBUFFER_VERSION;

  // SSPI
  bufs.ib[0].cbBuffer = sizes.cbSecurityTrailer;
  bufs.ib[0].BufferType = SECBUFFER_TOKEN;
  bufs.ib[0].pvBuffer = moz_xmalloc(sizes.cbSecurityTrailer);

  // APP Data
  bufs.ib[1].BufferType = SECBUFFER_DATA;
  bufs.ib[1].pvBuffer = moz_xmalloc(inTokenLen);
  bufs.ib[1].cbBuffer = inTokenLen;

  memcpy(bufs.ib[1].pvBuffer, inToken, inTokenLen);

  // SSPI
  bufs.ib[2].BufferType = SECBUFFER_PADDING;
  bufs.ib[2].cbBuffer = sizes.cbBlockSize;
  bufs.ib[2].pvBuffer = moz_xmalloc(bufs.ib[2].cbBuffer);

  rc = (sspi->EncryptMessage)(&mCtxt, confidential ? 0 : KERB_WRAP_NO_ENCRYPT,
                              &ibd, 0);

  if (SEC_SUCCESS(rc)) {
    int len = bufs.ib[0].cbBuffer + bufs.ib[1].cbBuffer + bufs.ib[2].cbBuffer;
    char* p = (char*)moz_xmalloc(len);

    *outToken = (void*)p;
    *outTokenLen = len;

    memcpy(p, bufs.ib[0].pvBuffer, bufs.ib[0].cbBuffer);
    p += bufs.ib[0].cbBuffer;

    memcpy(p, bufs.ib[1].pvBuffer, bufs.ib[1].cbBuffer);
    p += bufs.ib[1].cbBuffer;

    memcpy(p, bufs.ib[2].pvBuffer, bufs.ib[2].cbBuffer);

    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}
