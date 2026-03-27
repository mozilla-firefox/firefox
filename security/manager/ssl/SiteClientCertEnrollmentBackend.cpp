/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SiteClientCertEnrollmentBackend.h"

#include <algorithm>
#include <cstring>

#include "ScopedNSSTypes.h"
#include "cert.h"
#include "cryptohi.h"
#include "keyhi.h"
#include "mozilla/Base64.h"
#include "mozilla/Logging.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIX509CertDB.h"
#include "nsLiteralString.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nss.h"
#include "pk11pub.h"
#include "secasn1.h"
#include "secoid.h"

extern mozilla::LazyLogModule gPIPNSSLog;

namespace mozilla::psm {

NS_IMPL_ISUPPORTS(SiteClientCertEnrollmentBackend,
                  nsISiteClientCertEnrollmentBackend)

namespace {

constexpr auto kEnrollmentKeyNicknamePrefix =
    "site-client-cert-enrollment:"_ns;
constexpr auto kCsrPemHeader = "-----BEGIN CERTIFICATE REQUEST-----\n"_ns;
constexpr auto kCsrPemFooter = "-----END CERTIFICATE REQUEST-----\n"_ns;

nsresult BuildEcParams(SECItem& aParams) {
  SECOidData* oidData = SECOID_FindOIDByTag(SEC_OID_SECG_EC_SECP256R1);
  if (!oidData || oidData->oid.len > aParams.len - 2) {
    return NS_ERROR_FAILURE;
  }

  aParams.data[0] = SEC_ASN1_OBJECT_ID;
  aParams.data[1] = oidData->oid.len;
  memcpy(aParams.data + 2, oidData->oid.data, oidData->oid.len);
  aParams.len = oidData->oid.len + 2;
  return NS_OK;
}

nsresult EncodeRequestAsPem(const SECItem& aRequestDer, nsACString& aCsrPem) {
  nsDependentCSubstring derString(
      reinterpret_cast<const char*>(aRequestDer.data), aRequestDer.len);
  nsCString base64;
  nsresult rv = Base64Encode(derString, base64);
  if (NS_FAILED(rv)) {
    return rv;
  }

  aCsrPem.Assign(kCsrPemHeader);
  for (uint32_t offset = 0; offset < base64.Length(); offset += 64) {
    uint32_t chunkLength = std::min<uint32_t>(64, base64.Length() - offset);
    aCsrPem.Append(Substring(base64, offset, chunkLength));
    aCsrPem.Append('\n');
  }
  aCsrPem.Append(kCsrPemFooter);
  return NS_OK;
}

void DestroyPrivateKeyWithoutDestroyingPKCS11Object(SECKEYPrivateKey* aKey) {
  PK11_FreeSlot(aKey->pkcs11Slot);
  PORT_FreeArena(aKey->arena, PR_TRUE);
}

}  // namespace

SiteClientCertEnrollmentBackend::PendingEnrollment*
SiteClientCertEnrollmentBackend::FindPendingEnrollment(
    const nsACString& aRequestId) {
  for (auto& pendingEnrollment : mPendingEnrollments) {
    if (pendingEnrollment.mRequestId == aRequestId) {
      return &pendingEnrollment;
    }
  }
  return nullptr;
}

NS_IMETHODIMP
SiteClientCertEnrollmentBackend::CreateEnrollmentRequest(
    const nsACString& aSubjectCommonName, nsACString& aRequestId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }
  if (!NSS_IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (aSubjectCommonName.IsEmpty()) {
    return NS_ERROR_INVALID_ARG;
  }

  UniquePK11SlotInfo slot(PK11_GetInternalKeySlot());
  if (!slot) {
    return NS_ERROR_FAILURE;
  }

  uint8_t paramBuffer[12];
  SECItem ecdsaParams = {siBuffer, paramBuffer, sizeof(paramBuffer)};
  nsresult rv = BuildEcParams(ecdsaParams);
  if (NS_FAILED(rv)) {
    return rv;
  }

  SECKEYPublicKey* publicKeyRaw = nullptr;
  UniqueSECKEYPrivateKey privateKey(
      PK11_GenerateKeyPair(slot.get(), CKM_EC_KEY_PAIR_GEN, &ecdsaParams,
                           &publicKeyRaw, PR_TRUE, PR_TRUE, nullptr));
  if (!privateKey) {
    return NS_ERROR_FAILURE;
  }
  UniqueSECKEYPublicKey publicKey(publicKeyRaw);
  if (!publicKey) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString subjectNameString("CN="_ns);
  subjectNameString.Append(aSubjectCommonName);
  UniqueCERTName subjectName(CERT_AsciiToName(subjectNameString.get()));
  if (!subjectName) {
    return NS_ERROR_FAILURE;
  }

  UniqueCERTSubjectPublicKeyInfo spki(
      SECKEY_CreateSubjectPublicKeyInfo(publicKey.get()));
  if (!spki) {
    return NS_ERROR_FAILURE;
  }

  UniqueCERTCertificateRequest certRequest(
      CERT_CreateCertificateRequest(subjectName.get(), spki.get(), nullptr));
  if (!certRequest) {
    return NS_ERROR_FAILURE;
  }

  PLArenaPool* arena = certRequest->arena;
  SECItem requestInfoDer = {siBuffer, nullptr, 0};
  if (!SEC_ASN1EncodeItem(arena, &requestInfoDer, certRequest.get(),
                          CERT_CertificateRequestTemplate)) {
    return NS_ERROR_FAILURE;
  }

  SECItem signedRequestDer = {siBuffer, nullptr, 0};
  if (SEC_DerSignData(arena, &signedRequestDer, requestInfoDer.data,
                      requestInfoDer.len, privateKey.get(),
                      SEC_OID_ANSIX962_ECDSA_SHA256_SIGNATURE) != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString csrPem;
  rv = EncodeRequestAsPem(signedRequestDer, csrPem);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString requestId;
  requestId.AppendInt(++mNextRequestId);

  nsAutoCString nickname(kEnrollmentKeyNicknamePrefix);
  nickname.Append(requestId);
  (void)PK11_SetPrivateKeyNickname(privateKey.get(), nickname.get());

  aRequestId.Assign(requestId);
  mPendingEnrollments.AppendElement(PendingEnrollment(
      std::move(requestId), std::move(csrPem), std::move(privateKey)));
  return NS_OK;
}

NS_IMETHODIMP
SiteClientCertEnrollmentBackend::GetEnrollmentRequestCsr(
    const nsACString& aRequestId, nsACString& aCsrPem) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  PendingEnrollment* pendingEnrollment = FindPendingEnrollment(aRequestId);
  if (!pendingEnrollment) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  aCsrPem = pendingEnrollment->mCsrPem;
  return NS_OK;
}

NS_IMETHODIMP
SiteClientCertEnrollmentBackend::CompleteEnrollment(
    const nsACString& aRequestId, const nsTArray<uint8_t>& aCertificateBytes) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }
  if (aCertificateBytes.IsEmpty()) {
    return NS_ERROR_INVALID_ARG;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("SiteClientCertEnrollmentBackend::CompleteEnrollment: entered "
           "requestId='%s' certificateBytesLength=%zu",
           PromiseFlatCString(aRequestId).get(),
           static_cast<size_t>(aCertificateBytes.Length())));

  PendingEnrollment* pendingEnrollment = FindPendingEnrollment(aRequestId);
  if (!pendingEnrollment) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("SiteClientCertEnrollmentBackend::CompleteEnrollment: no pending "
             "enrollment found for requestId='%s'",
             PromiseFlatCString(aRequestId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIX509CertDB> certDB(do_GetService(NS_X509CERTDB_CONTRACTID));
  if (!certDB) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("SiteClientCertEnrollmentBackend::CompleteEnrollment: failed to "
             "get cert DB service for requestId='%s'",
             PromiseFlatCString(aRequestId).get()));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("SiteClientCertEnrollmentBackend::CompleteEnrollment: calling "
           "ImportUserCertificate for requestId='%s'",
           PromiseFlatCString(aRequestId).get()));
  nsresult rv = certDB->ImportUserCertificate(
      const_cast<uint8_t*>(aCertificateBytes.Elements()),
      aCertificateBytes.Length(), nullptr);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("SiteClientCertEnrollmentBackend::CompleteEnrollment: "
             "ImportUserCertificate failed for requestId='%s' rv=0x%08" PRIx32,
             PromiseFlatCString(aRequestId).get(), static_cast<uint32_t>(rv)));
    return rv;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("SiteClientCertEnrollmentBackend::CompleteEnrollment: "
           "ImportUserCertificate succeeded for requestId='%s'",
           PromiseFlatCString(aRequestId).get()));

  for (uint32_t index = 0; index < mPendingEnrollments.Length(); index++) {
    if (mPendingEnrollments[index].mRequestId == aRequestId) {
      mPendingEnrollments.RemoveElementAt(index);
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("SiteClientCertEnrollmentBackend::CompleteEnrollment: removed "
               "pending enrollment for requestId='%s'",
               PromiseFlatCString(aRequestId).get()));
      return NS_OK;
    }
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("SiteClientCertEnrollmentBackend::CompleteEnrollment: completed "
           "without removing pending enrollment for requestId='%s'",
           PromiseFlatCString(aRequestId).get()));
  return NS_OK;
}

NS_IMETHODIMP
SiteClientCertEnrollmentBackend::AbortEnrollment(const nsACString& aRequestId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  for (uint32_t index = 0; index < mPendingEnrollments.Length(); index++) {
    if (mPendingEnrollments[index].mRequestId != aRequestId) {
      continue;
    }

    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("SiteClientCertEnrollmentBackend::AbortEnrollment: deleting "
             "private key for requestId='%s'",
             PromiseFlatCString(aRequestId).get()));
    SECKEYPrivateKey* privateKey =
        mPendingEnrollments[index].mPrivateKey.release();
    SECStatus srv = PK11_DeleteTokenPrivateKey(privateKey, false);
    DestroyPrivateKeyWithoutDestroyingPKCS11Object(privateKey);
    mPendingEnrollments.RemoveElementAt(index);
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("SiteClientCertEnrollmentBackend::AbortEnrollment: delete result for "
         "requestId='%s' srv=%d",
         PromiseFlatCString(aRequestId).get(), static_cast<int>(srv)));
    return srv == SECSuccess ? NS_OK : NS_ERROR_FAILURE;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("SiteClientCertEnrollmentBackend::AbortEnrollment: no pending "
           "enrollment found for requestId='%s'",
           PromiseFlatCString(aRequestId).get()));
  return NS_ERROR_NOT_AVAILABLE;
}

}  // namespace mozilla::psm
