/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WinWebAuthnService.h"

#include "WebAuthnAutoFillEntry.h"
#include "WebAuthnEnumStrings.h"
#include "WebAuthnResult.h"
#include "WebAuthnTransportIdentifiers.h"
#include "mozilla/Assertions.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/dom/PWebAuthnTransactionParent.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsTextFormatter.h"
#include "nsWindowsHelpers.h"
#include "winwebauthn/webauthn.h"

namespace mozilla::dom {

namespace {
StaticRWLock gWinWebAuthnModuleLock;

static bool gWinWebAuthnModuleUnusable = false;
static HMODULE gWinWebAuthnModule = 0;

static const LPCWSTR gWebAuthnHintStrings[3] = {
    WEBAUTHN_CREDENTIAL_HINT_SECURITY_KEY,
    WEBAUTHN_CREDENTIAL_HINT_CLIENT_DEVICE, WEBAUTHN_CREDENTIAL_HINT_HYBRID};

static decltype(WebAuthNIsUserVerifyingPlatformAuthenticatorAvailable)*
    gWinWebauthnIsUVPAA = nullptr;
static decltype(WebAuthNAuthenticatorMakeCredential)*
    gWinWebauthnMakeCredential = nullptr;
static decltype(WebAuthNFreeCredentialAttestation)*
    gWinWebauthnFreeCredentialAttestation = nullptr;
static decltype(WebAuthNAuthenticatorGetAssertion)* gWinWebauthnGetAssertion =
    nullptr;
static decltype(WebAuthNFreeAssertion)* gWinWebauthnFreeAssertion = nullptr;
static decltype(WebAuthNGetCancellationId)* gWinWebauthnGetCancellationId =
    nullptr;
static decltype(WebAuthNCancelCurrentOperation)*
    gWinWebauthnCancelCurrentOperation = nullptr;
static decltype(WebAuthNGetErrorName)* gWinWebauthnGetErrorName = nullptr;
static decltype(WebAuthNGetApiVersionNumber)* gWinWebauthnGetApiVersionNumber =
    nullptr;
static decltype(WebAuthNGetPlatformCredentialList)*
    gWinWebauthnGetPlatformCredentialList = nullptr;
static decltype(WebAuthNFreePlatformCredentialList)*
    gWinWebauthnFreePlatformCredentialList = nullptr;
}  // namespace

/***********************************************************************
 * WinWebAuthnService Implementation
 **********************************************************************/

constexpr uint32_t kMinWinWebAuthNApiVersion = WEBAUTHN_API_VERSION_1;

NS_IMPL_ISUPPORTS(WinWebAuthnService, nsIWebAuthnService)

/* static */
nsresult WinWebAuthnService::EnsureWinWebAuthnModuleLoaded() {
  {
    StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
    if (gWinWebAuthnModule) {
      // The module is already loaded.
      return NS_OK;
    }
    if (gWinWebAuthnModuleUnusable) {
      // A previous attempt to load the module failed.
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  StaticAutoWriteLock lock(gWinWebAuthnModuleLock);
  if (gWinWebAuthnModule) {
    // Another thread successfully loaded the module while we were waiting.
    return NS_OK;
  }
  if (gWinWebAuthnModuleUnusable) {
    // Another thread failed to load the module while we were waiting.
    return NS_ERROR_NOT_AVAILABLE;
  }

  gWinWebAuthnModule = LoadLibrarySystem32(L"webauthn.dll");
  auto markModuleUnusable = MakeScopeExit([]() {
    if (gWinWebAuthnModule) {
      FreeLibrary(gWinWebAuthnModule);
      gWinWebAuthnModule = 0;
    }
    gWinWebAuthnModuleUnusable = true;
  });

  if (!gWinWebAuthnModule) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  gWinWebauthnIsUVPAA = reinterpret_cast<
      decltype(WebAuthNIsUserVerifyingPlatformAuthenticatorAvailable)*>(
      GetProcAddress(gWinWebAuthnModule,
                     "WebAuthNIsUserVerifyingPlatformAuthenticatorAvailable"));
  gWinWebauthnMakeCredential =
      reinterpret_cast<decltype(WebAuthNAuthenticatorMakeCredential)*>(
          GetProcAddress(gWinWebAuthnModule,
                         "WebAuthNAuthenticatorMakeCredential"));
  gWinWebauthnFreeCredentialAttestation =
      reinterpret_cast<decltype(WebAuthNFreeCredentialAttestation)*>(
          GetProcAddress(gWinWebAuthnModule,
                         "WebAuthNFreeCredentialAttestation"));
  gWinWebauthnGetAssertion =
      reinterpret_cast<decltype(WebAuthNAuthenticatorGetAssertion)*>(
          GetProcAddress(gWinWebAuthnModule,
                         "WebAuthNAuthenticatorGetAssertion"));
  gWinWebauthnFreeAssertion =
      reinterpret_cast<decltype(WebAuthNFreeAssertion)*>(
          GetProcAddress(gWinWebAuthnModule, "WebAuthNFreeAssertion"));
  gWinWebauthnGetCancellationId =
      reinterpret_cast<decltype(WebAuthNGetCancellationId)*>(
          GetProcAddress(gWinWebAuthnModule, "WebAuthNGetCancellationId"));
  gWinWebauthnCancelCurrentOperation =
      reinterpret_cast<decltype(WebAuthNCancelCurrentOperation)*>(
          GetProcAddress(gWinWebAuthnModule, "WebAuthNCancelCurrentOperation"));
  gWinWebauthnGetErrorName = reinterpret_cast<decltype(WebAuthNGetErrorName)*>(
      GetProcAddress(gWinWebAuthnModule, "WebAuthNGetErrorName"));
  gWinWebauthnGetApiVersionNumber =
      reinterpret_cast<decltype(WebAuthNGetApiVersionNumber)*>(
          GetProcAddress(gWinWebAuthnModule, "WebAuthNGetApiVersionNumber"));

  if (!(gWinWebauthnIsUVPAA && gWinWebauthnMakeCredential &&
        gWinWebauthnFreeCredentialAttestation && gWinWebauthnGetAssertion &&
        gWinWebauthnFreeAssertion && gWinWebauthnGetCancellationId &&
        gWinWebauthnCancelCurrentOperation && gWinWebauthnGetErrorName &&
        gWinWebauthnGetApiVersionNumber)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  DWORD version = gWinWebauthnGetApiVersionNumber();

  if (version >= WEBAUTHN_API_VERSION_4) {
    gWinWebauthnGetPlatformCredentialList =
        reinterpret_cast<decltype(WebAuthNGetPlatformCredentialList)*>(
            GetProcAddress(gWinWebAuthnModule,
                           "WebAuthNGetPlatformCredentialList"));
    gWinWebauthnFreePlatformCredentialList =
        reinterpret_cast<decltype(WebAuthNFreePlatformCredentialList)*>(
            GetProcAddress(gWinWebAuthnModule,
                           "WebAuthNFreePlatformCredentialList"));
    if (!(gWinWebauthnGetPlatformCredentialList &&
          gWinWebauthnFreePlatformCredentialList)) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  // Bug 1869584: In some of our tests, a content process can end up here due to
  // a call to WinWebAuthnService::AreWebAuthNApisAvailable. This causes us to
  // fail an assertion in Preferences::SetBool, which is parent-process only.
  if (XRE_IsParentProcess()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(__func__, [version]() {
      Preferences::SetBool("security.webauthn.show_ms_settings_link",
                           version >= WEBAUTHN_API_VERSION_7);
    }));
  }

  markModuleUnusable.release();
  return NS_OK;
}

WinWebAuthnService::~WinWebAuthnService() {
  StaticAutoWriteLock lock(gWinWebAuthnModuleLock);
  if (gWinWebAuthnModule) {
    FreeLibrary(gWinWebAuthnModule);
  }
  gWinWebAuthnModule = 0;
}

// static
void PrunePublicKeyCredentialHints(const nsTArray<nsString>& aInHints,
                                   /* out */ nsTArray<LPCWSTR>& aOutHints) {
  for (const nsString& inputHint : aInHints) {
    for (const LPCWSTR knownHint : gWebAuthnHintStrings) {
      if (inputHint.Equals(knownHint)) {
        aOutHints.AppendElement(knownHint);
      }
    }
  }
}

// static
bool WinWebAuthnService::AreWebAuthNApisAvailable() {
  nsresult rv = EnsureWinWebAuthnModuleLoaded();
  NS_ENSURE_SUCCESS(rv, false);

  StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
  return gWinWebAuthnModule &&
         gWinWebauthnGetApiVersionNumber() >= kMinWinWebAuthNApiVersion;
}

NS_IMETHODIMP
WinWebAuthnService::GetIsUVPAA(bool* aAvailable) {
  nsresult rv = EnsureWinWebAuthnModuleLoaded();
  NS_ENSURE_SUCCESS(rv, rv);

  if (WinWebAuthnService::AreWebAuthNApisAvailable()) {
    BOOL isUVPAA = FALSE;
    StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
    *aAvailable = gWinWebAuthnModule && gWinWebauthnIsUVPAA(&isUVPAA) == S_OK &&
                  isUVPAA == TRUE;
  } else {
    *aAvailable = false;
  }
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::Cancel(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mActiveTransaction.isSome() &&
      mActiveTransaction.ref().transactionId == aTransactionId) {
    Reset();
  }
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::Reset() {
  // Reset will never be the first function to use gWinWebAuthnModule, so
  // we shouldn't try to initialize it here.
  MOZ_ASSERT(NS_IsMainThread());
  if (mActiveTransaction.isSome()) {
    StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
    if (gWinWebAuthnModule) {
      const GUID cancellationId = mActiveTransaction.ref().cancellationId;
      gWinWebauthnCancelCurrentOperation(&cancellationId);
    }
    mActiveTransaction.reset();
  }
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::MakeCredential(uint64_t aTransactionId,
                                   uint64_t aBrowsingContextId,
                                   nsIWebAuthnRegisterArgs* aArgs,
                                   nsIWebAuthnRegisterPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  nsresult rv = EnsureWinWebAuthnModuleLoaded();
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(mActiveTransaction.isNothing(),
             "WebAuthnService should reset the platform service before "
             "dispatching MakeCredential");
  StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
  GUID cancellationId;
  if (gWinWebauthnGetCancellationId(&cancellationId) != S_OK) {
    // caller will reject promise
    return NS_ERROR_DOM_UNKNOWN_ERR;
  }
  mActiveTransaction = Some(TransactionState{aTransactionId, cancellationId});

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "WinWebAuthnService::MakeCredential",
      [self = RefPtr{this}, aArgs = RefPtr{aArgs}, aPromise = RefPtr{aPromise},
       cancellationId]() mutable {
        // Take a read lock on gWinWebAuthnModuleLock to prevent the module from
        // being unloaded while the operation is in progress. This does not
        // prevent the operation from being cancelled, so it does not block a
        // clean shutdown.
        StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
        if (!gWinWebAuthnModule) {
          aPromise->Reject(NS_ERROR_DOM_UNKNOWN_ERR);
          return;
        }

        // RP Information
        nsString rpId;
        (void)aArgs->GetRpId(rpId);
        WEBAUTHN_RP_ENTITY_INFORMATION rpInfo = {
            WEBAUTHN_RP_ENTITY_INFORMATION_CURRENT_VERSION, rpId.get(), nullptr,
            nullptr};

        // User Information
        WEBAUTHN_USER_ENTITY_INFORMATION userInfo = {
            WEBAUTHN_USER_ENTITY_INFORMATION_CURRENT_VERSION,
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr};

        // Client Data
        nsCString clientDataJSON;
        (void)aArgs->GetClientDataJSON(clientDataJSON);
        WEBAUTHN_CLIENT_DATA WebAuthNClientData = {
            WEBAUTHN_CLIENT_DATA_CURRENT_VERSION,
            (DWORD)clientDataJSON.Length(), (BYTE*)(clientDataJSON.get()),
            WEBAUTHN_HASH_ALGORITHM_SHA_256};

        // User Verification Requirement
        DWORD winUserVerificationReq =
            WEBAUTHN_USER_VERIFICATION_REQUIREMENT_ANY;

        // Resident Key Requirement.
        BOOL winRequireResidentKey = FALSE;  // Will be set to TRUE if and only
                                             // if residentKey = "required"
        BOOL winPreferResidentKey = FALSE;   // Will be set to TRUE if and only
                                             // if residentKey = "preferred"

        // AttestationConveyance
        DWORD winAttestation = WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_ANY;

        // Large Blob
        DWORD largeBlobSupport = WEBAUTHN_LARGE_BLOB_SUPPORT_NONE;
        bool largeBlobSupportRequired;
        nsresult rv =
            aArgs->GetLargeBlobSupportRequired(&largeBlobSupportRequired);
        if (rv != NS_ERROR_NOT_AVAILABLE) {
          if (NS_FAILED(rv)) {
            aPromise->Reject(rv);
            return;
          }
          if (largeBlobSupportRequired) {
            largeBlobSupport = WEBAUTHN_LARGE_BLOB_SUPPORT_REQUIRED;
          } else {
            largeBlobSupport = WEBAUTHN_LARGE_BLOB_SUPPORT_PREFERRED;
          }
        }

        // Prf
        BOOL winEnablePrf = FALSE;

        nsString rpName;
        (void)aArgs->GetRpName(rpName);
        rpInfo.pwszName = rpName.get();
        rpInfo.pwszIcon = nullptr;

        nsTArray<uint8_t> userId;
        (void)aArgs->GetUserId(userId);
        userInfo.cbId = static_cast<DWORD>(userId.Length());
        userInfo.pbId = const_cast<unsigned char*>(userId.Elements());

        nsString userName;
        (void)aArgs->GetUserName(userName);
        userInfo.pwszName = userName.get();

        userInfo.pwszIcon = nullptr;

        nsString userDisplayName;
        (void)aArgs->GetUserDisplayName(userDisplayName);
        userInfo.pwszDisplayName = userDisplayName.get();

        // Algorithms
        nsTArray<WEBAUTHN_COSE_CREDENTIAL_PARAMETER> coseParams;
        nsTArray<int32_t> coseAlgs;
        (void)aArgs->GetCoseAlgs(coseAlgs);
        for (const int32_t& coseAlg : coseAlgs) {
          WEBAUTHN_COSE_CREDENTIAL_PARAMETER coseAlgorithm = {
              WEBAUTHN_COSE_CREDENTIAL_PARAMETER_CURRENT_VERSION,
              WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY, coseAlg};
          coseParams.AppendElement(coseAlgorithm);
        }

        nsString userVerificationReq;
        (void)aArgs->GetUserVerification(userVerificationReq);
        // This mapping needs to be reviewed if values are added to the
        // UserVerificationRequirement enum.
        static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
        if (userVerificationReq.EqualsLiteral(
                MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED)) {
          winUserVerificationReq =
              WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED;
        } else if (userVerificationReq.EqualsLiteral(
                       MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED)) {
          winUserVerificationReq =
              WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED;
        } else if (userVerificationReq.EqualsLiteral(
                       MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_DISCOURAGED)) {
          winUserVerificationReq =
              WEBAUTHN_USER_VERIFICATION_REQUIREMENT_DISCOURAGED;
        } else {
          winUserVerificationReq = WEBAUTHN_USER_VERIFICATION_REQUIREMENT_ANY;
        }

        // Attachment
        DWORD winAttachment = WEBAUTHN_AUTHENTICATOR_ATTACHMENT_ANY;
        nsString authenticatorAttachment;
        rv = aArgs->GetAuthenticatorAttachment(authenticatorAttachment);
        if (rv != NS_ERROR_NOT_AVAILABLE) {
          if (NS_FAILED(rv)) {
            aPromise->Reject(rv);
            return;
          }
          // This mapping needs to be reviewed if values are added to the
          // AuthenticatorAttachement enum.
          static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
          if (authenticatorAttachment.EqualsLiteral(
                  MOZ_WEBAUTHN_AUTHENTICATOR_ATTACHMENT_PLATFORM)) {
            winAttachment = WEBAUTHN_AUTHENTICATOR_ATTACHMENT_PLATFORM;
          } else if (
              authenticatorAttachment.EqualsLiteral(
                  MOZ_WEBAUTHN_AUTHENTICATOR_ATTACHMENT_CROSS_PLATFORM)) {
            winAttachment = WEBAUTHN_AUTHENTICATOR_ATTACHMENT_CROSS_PLATFORM;
          } else {
            winAttachment = WEBAUTHN_AUTHENTICATOR_ATTACHMENT_ANY;
          }
        }

        nsString residentKey;
        (void)aArgs->GetResidentKey(residentKey);
        // This mapping needs to be reviewed if values are added to the
        // ResidentKeyRequirement enum.
        static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
        if (residentKey.EqualsLiteral(
                MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_REQUIRED)) {
          winRequireResidentKey = TRUE;
          winPreferResidentKey = FALSE;
        } else if (residentKey.EqualsLiteral(
                       MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_PREFERRED)) {
          winRequireResidentKey = FALSE;
          winPreferResidentKey = TRUE;
        } else if (residentKey.EqualsLiteral(
                       MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_DISCOURAGED)) {
          winRequireResidentKey = FALSE;
          winPreferResidentKey = FALSE;
        } else {
          // WebAuthnHandler::MakeCredential is supposed to assign one of the
          // above values, so this shouldn't happen.
          MOZ_ASSERT_UNREACHABLE();
          aPromise->Reject(NS_ERROR_DOM_UNKNOWN_ERR);
          return;
        }

        // AttestationConveyance
        nsString attestation;
        (void)aArgs->GetAttestationConveyancePreference(attestation);
        // This mapping needs to be reviewed if values are added to the
        // AttestationConveyancePreference enum.
        static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
        if (attestation.EqualsLiteral(
                MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_NONE)) {
          winAttestation = WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_NONE;
        } else if (
            attestation.EqualsLiteral(
                MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_INDIRECT)) {
          winAttestation = WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_INDIRECT;
        } else if (attestation.EqualsLiteral(
                       MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_DIRECT)) {
          winAttestation = WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_DIRECT;
        } else {
          winAttestation = WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_ANY;
        }

        // Extensions that might require an entry in the extensions array:
        // credProtect, hmac-secret, minPinLength.
        nsTArray<WEBAUTHN_EXTENSION> rgExtension(3);
        WEBAUTHN_CRED_PROTECT_EXTENSION_IN winCredProtect = {
            .dwCredProtect = WEBAUTHN_USER_VERIFICATION_ANY,
            .bRequireCredProtect = FALSE,
        };
        BOOL winHmacCreateSecret = FALSE;
        BOOL winMinPinLength = FALSE;

        nsCString credProtectPolicy;
        if (NS_SUCCEEDED(
                aArgs->GetCredentialProtectionPolicy(credProtectPolicy))) {
          Maybe<CredentialProtectionPolicy> policy(
              StringToEnum<CredentialProtectionPolicy>(credProtectPolicy));
          if (policy.isNothing()) {
            aPromise->Reject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
            return;
          }
          switch (policy.ref()) {
            case CredentialProtectionPolicy::UserVerificationOptional:
              winCredProtect.dwCredProtect =
                  WEBAUTHN_USER_VERIFICATION_OPTIONAL;
              break;
            case CredentialProtectionPolicy::
                UserVerificationOptionalWithCredentialIDList:
              winCredProtect.dwCredProtect =
                  WEBAUTHN_USER_VERIFICATION_OPTIONAL_WITH_CREDENTIAL_ID_LIST;
              break;
            case CredentialProtectionPolicy::UserVerificationRequired:
              winCredProtect.dwCredProtect =
                  WEBAUTHN_USER_VERIFICATION_REQUIRED;
              break;
          }

          bool enforceCredProtectPolicy;
          if (NS_SUCCEEDED(aArgs->GetEnforceCredentialProtectionPolicy(
                  &enforceCredProtectPolicy)) &&
              enforceCredProtectPolicy) {
            winCredProtect.bRequireCredProtect = TRUE;
          }

          rgExtension.AppendElement(WEBAUTHN_EXTENSION{
              .pwszExtensionIdentifier =
                  WEBAUTHN_EXTENSIONS_IDENTIFIER_CRED_PROTECT,
              .cbExtension = sizeof(WEBAUTHN_CRED_PROTECT_EXTENSION_IN),
              .pvExtension = &winCredProtect,
          });
        }

        bool requestedPrf;
        bool requestedHmacCreateSecret;
        if (NS_SUCCEEDED(aArgs->GetPrf(&requestedPrf)) &&
            NS_SUCCEEDED(
                aArgs->GetHmacCreateSecret(&requestedHmacCreateSecret)) &&
            (requestedPrf || requestedHmacCreateSecret)) {
          winEnablePrf = requestedPrf ? TRUE : FALSE;
          winHmacCreateSecret = TRUE;
          rgExtension.AppendElement(WEBAUTHN_EXTENSION{
              .pwszExtensionIdentifier =
                  WEBAUTHN_EXTENSIONS_IDENTIFIER_HMAC_SECRET,
              .cbExtension = sizeof(BOOL),
              .pvExtension = &winHmacCreateSecret,
          });
        }

        nsTArray<uint8_t> prfEvalFirst;
        nsTArray<uint8_t> prfEvalSecond;
        WEBAUTHN_HMAC_SECRET_SALT prfGlobalEval = {0};
        PWEBAUTHN_HMAC_SECRET_SALT pPrfGlobalEval = NULL;
        if (requestedPrf) {
          pPrfGlobalEval = &prfGlobalEval;
          rv = aArgs->GetPrfEvalFirst(prfEvalFirst);
          if (rv == NS_OK) {
            prfGlobalEval.cbFirst = prfEvalFirst.Length();
            prfGlobalEval.pbFirst = prfEvalFirst.Elements();
          }
          rv = aArgs->GetPrfEvalSecond(prfEvalSecond);
          if (rv == NS_OK) {
            prfGlobalEval.cbSecond = prfEvalSecond.Length();
            prfGlobalEval.pbSecond = prfEvalSecond.Elements();
          }
        }

        bool requestedMinPinLength;
        if (NS_SUCCEEDED(aArgs->GetMinPinLength(&requestedMinPinLength)) &&
            requestedMinPinLength) {
          winMinPinLength = TRUE;
          rgExtension.AppendElement(WEBAUTHN_EXTENSION{
              .pwszExtensionIdentifier =
                  WEBAUTHN_EXTENSIONS_IDENTIFIER_MIN_PIN_LENGTH,
              .cbExtension = sizeof(BOOL),
              .pvExtension = &winMinPinLength,
          });
        }

        WEBAUTHN_COSE_CREDENTIAL_PARAMETERS WebAuthNCredentialParameters = {
            static_cast<DWORD>(coseParams.Length()), coseParams.Elements()};

        // Exclude Credentials
        nsTArray<nsTArray<uint8_t>> excludeList;
        (void)aArgs->GetExcludeList(excludeList);

        nsTArray<uint8_t> excludeListTransports;
        (void)aArgs->GetExcludeListTransports(excludeListTransports);

        if (excludeList.Length() != excludeListTransports.Length()) {
          aPromise->Reject(NS_ERROR_DOM_UNKNOWN_ERR);
          return;
        }

        nsTArray<WEBAUTHN_CREDENTIAL_EX> excludeCredentials;
        WEBAUTHN_CREDENTIAL_EX* pExcludeCredentials = nullptr;
        nsTArray<WEBAUTHN_CREDENTIAL_EX*> excludeCredentialsPtrs;
        WEBAUTHN_CREDENTIAL_LIST excludeCredentialList = {0};
        WEBAUTHN_CREDENTIAL_LIST* pExcludeCredentialList = nullptr;

        for (size_t i = 0; i < excludeList.Length(); i++) {
          nsTArray<uint8_t>& cred = excludeList[i];
          uint8_t& transports = excludeListTransports[i];
          DWORD winTransports = 0;
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_USB) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_USB;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_NFC) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_NFC;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_BLE) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_BLE;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_INTERNAL) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_INTERNAL;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_HYBRID) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_HYBRID;
          }

          WEBAUTHN_CREDENTIAL_EX credential = {
              WEBAUTHN_CREDENTIAL_EX_CURRENT_VERSION,
              static_cast<DWORD>(cred.Length()), (PBYTE)(cred.Elements()),
              WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY, winTransports};
          excludeCredentials.AppendElement(credential);
        }

        if (!excludeCredentials.IsEmpty()) {
          pExcludeCredentials = excludeCredentials.Elements();
          for (DWORD i = 0; i < excludeCredentials.Length(); i++) {
            excludeCredentialsPtrs.AppendElement(&pExcludeCredentials[i]);
          }
          excludeCredentialList.cCredentials = excludeCredentials.Length();
          excludeCredentialList.ppCredentials =
              excludeCredentialsPtrs.Elements();
          pExcludeCredentialList = &excludeCredentialList;
        }

        uint32_t timeout_u32;
        (void)aArgs->GetTimeoutMS(&timeout_u32);
        DWORD timeout = timeout_u32;

        bool privateBrowsing;
        (void)aArgs->GetPrivateBrowsing(&privateBrowsing);
        BOOL winPrivateBrowsing = FALSE;
        if (privateBrowsing) {
          winPrivateBrowsing = TRUE;
        }

        nsTArray<nsString> inputHints;
        (void)aArgs->GetHints(inputHints);

        nsTArray<LPCWSTR> hints;
        PrunePublicKeyCredentialHints(inputHints, hints);

        // MakeCredentialOptions
        WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS
        WebAuthNCredentialOptions = {
            WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_8,
            timeout,
            {0, NULL},
            {0, NULL},
            winAttachment,
            winRequireResidentKey,
            winUserVerificationReq,
            winAttestation,
            0,                // Flags
            &cancellationId,  // CancellationId
            pExcludeCredentialList,
            WEBAUTHN_ENTERPRISE_ATTESTATION_NONE,
            largeBlobSupport,       // LargeBlobSupport
            winPreferResidentKey,   // PreferResidentKey
            winPrivateBrowsing,     // BrowserInPrivateMode
            winEnablePrf,           // EnablePrf
            NULL,                   // LinkedDevice
            0,                      // size of JsonExt
            NULL,                   // JsonExt
            pPrfGlobalEval,         // PRFGlobalEval
            (DWORD)hints.Length(),  // Size of CredentialHints
            hints.Elements(),       // CredentialHints
        };

        if (rgExtension.Length() != 0) {
          WebAuthNCredentialOptions.Extensions.cExtensions =
              rgExtension.Length();
          WebAuthNCredentialOptions.Extensions.pExtensions =
              rgExtension.Elements();
        }

        PWEBAUTHN_CREDENTIAL_ATTESTATION pWebAuthNCredentialAttestation =
            nullptr;

        // Bug 1518876: Get Window Handle from Content process for Windows
        // WebAuthN APIs
        HWND hWnd = GetForegroundWindow();

        HRESULT hr = gWinWebauthnMakeCredential(
            hWnd, &rpInfo, &userInfo, &WebAuthNCredentialParameters,
            &WebAuthNClientData, &WebAuthNCredentialOptions,
            &pWebAuthNCredentialAttestation);

        if (hr == S_OK) {
          RefPtr<WebAuthnRegisterResult> result = new WebAuthnRegisterResult(
              clientDataJSON, pWebAuthNCredentialAttestation);

          // WEBAUTHN_CREDENTIAL_ATTESTATION structs of version >= 4 always
          // include a flag to indicate whether a resident key was created. We
          // copy that flag to the credProps extension output only if the RP
          // requested the credProps extension.
          bool requestedCredProps;
          (void)aArgs->GetCredProps(&requestedCredProps);
          if (requestedCredProps &&
              pWebAuthNCredentialAttestation->dwVersion >=
                  WEBAUTHN_CREDENTIAL_ATTESTATION_VERSION_4) {
            BOOL rk = pWebAuthNCredentialAttestation->bResidentKey;
            (void)result->SetCredPropsRk(rk == TRUE);
          }
          gWinWebauthnFreeCredentialAttestation(pWebAuthNCredentialAttestation);

          aPromise->Resolve(result);
        } else {
          PCWSTR errorName = gWinWebauthnGetErrorName(hr);
          nsresult aError = NS_ERROR_DOM_ABORT_ERR;

          if (_wcsicmp(errorName, L"InvalidStateError") == 0) {
            aError = NS_ERROR_DOM_INVALID_STATE_ERR;
          } else if (_wcsicmp(errorName, L"ConstraintError") == 0 ||
                     _wcsicmp(errorName, L"UnknownError") == 0) {
            aError = NS_ERROR_DOM_UNKNOWN_ERR;
          } else if (_wcsicmp(errorName, L"NotSupportedError") == 0) {
            aError = NS_ERROR_DOM_INVALID_STATE_ERR;
          } else if (_wcsicmp(errorName, L"NotAllowedError") == 0) {
            aError = NS_ERROR_DOM_NOT_ALLOWED_ERR;
          }

          aPromise->Reject(aError);
        }
      }));

  NS_DispatchBackgroundTask(runnable, NS_DISPATCH_EVENT_MAY_BLOCK);
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::GetAssertion(uint64_t aTransactionId,
                                 uint64_t aBrowsingContextId,
                                 nsIWebAuthnSignArgs* aArgs,
                                 nsIWebAuthnSignPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  nsresult rv = EnsureWinWebAuthnModuleLoaded();
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(mActiveTransaction.isNothing(),
             "WebAuthnService should reset the platform service before "
             "dispatching GetAssertion");

  GUID cancellationId;
  {
    StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
    if (gWinWebauthnGetCancellationId(&cancellationId) != S_OK) {
      // caller will reject promise
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }
  }

  mActiveTransaction = Some(TransactionState{aTransactionId, cancellationId});

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "WinWebAuthnService::GetAssertion",
      [aArgs = RefPtr{aArgs}, aPromise = RefPtr{aPromise},
       aCancellationId = cancellationId]() mutable {
        // Take a read lock on gWinWebAuthnModuleLock to prevent the module from
        // being unloaded while the operation is in progress. This does not
        // prevent the operation from being cancelled, so it does not block a
        // clean shutdown.
        StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
        if (!gWinWebAuthnModule) {
          aPromise->Reject(NS_ERROR_DOM_UNKNOWN_ERR);
          return;
        }

        // Attachment
        DWORD winAttachment = WEBAUTHN_AUTHENTICATOR_ATTACHMENT_ANY;

        // AppId
        BOOL bAppIdUsed = FALSE;
        BOOL* pbAppIdUsed = nullptr;
        PCWSTR winAppIdentifier = nullptr;

        // Client Data
        nsCString clientDataJSON;
        (void)aArgs->GetClientDataJSON(clientDataJSON);
        WEBAUTHN_CLIENT_DATA WebAuthNClientData = {
            WEBAUTHN_CLIENT_DATA_CURRENT_VERSION,
            (DWORD)clientDataJSON.Length(), (BYTE*)(clientDataJSON.get()),
            WEBAUTHN_HASH_ALGORITHM_SHA_256};

        nsString appId;
        nsresult rv = aArgs->GetAppId(appId);
        if (rv != NS_ERROR_NOT_AVAILABLE) {
          if (NS_FAILED(rv)) {
            aPromise->Reject(rv);
            return;
          }
          winAppIdentifier = appId.get();
          pbAppIdUsed = &bAppIdUsed;
        }

        // RPID
        nsString rpId;
        (void)aArgs->GetRpId(rpId);

        // User Verification Requirement
        nsString userVerificationReq;
        (void)aArgs->GetUserVerification(userVerificationReq);
        DWORD winUserVerificationReq;
        // This mapping needs to be reviewed if values are added to the
        // UserVerificationRequirement enum.
        static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
        if (userVerificationReq.EqualsLiteral(
                MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED)) {
          winUserVerificationReq =
              WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED;
        } else if (userVerificationReq.EqualsLiteral(
                       MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED)) {
          winUserVerificationReq =
              WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED;
        } else if (userVerificationReq.EqualsLiteral(
                       MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_DISCOURAGED)) {
          winUserVerificationReq =
              WEBAUTHN_USER_VERIFICATION_REQUIREMENT_DISCOURAGED;
        } else {
          winUserVerificationReq = WEBAUTHN_USER_VERIFICATION_REQUIREMENT_ANY;
        }

        // Large Blob
        DWORD credLargeBlobOperation = WEBAUTHN_CRED_LARGE_BLOB_OPERATION_NONE;
        DWORD credLargeBlobSize = 0;
        PBYTE credLargeBlob = nullptr;
        nsTArray<uint8_t> largeBlobWrite;
        bool largeBlobRead;
        rv = aArgs->GetLargeBlobRead(&largeBlobRead);
        if (rv != NS_ERROR_NOT_AVAILABLE) {
          if (NS_FAILED(rv)) {
            aPromise->Reject(rv);
            return;
          }
          if (largeBlobRead) {
            credLargeBlobOperation = WEBAUTHN_CRED_LARGE_BLOB_OPERATION_GET;
          } else {
            rv = aArgs->GetLargeBlobWrite(largeBlobWrite);
            if (rv != NS_ERROR_NOT_AVAILABLE && NS_FAILED(rv)) {
              aPromise->Reject(rv);
              return;
            }
            credLargeBlobOperation = WEBAUTHN_CRED_LARGE_BLOB_OPERATION_SET;
            credLargeBlobSize = largeBlobWrite.Length();
            credLargeBlob = largeBlobWrite.Elements();
          }
        }

        // PRF inputs
        WEBAUTHN_HMAC_SECRET_SALT_VALUES* pPrfInputs = nullptr;
        WEBAUTHN_HMAC_SECRET_SALT_VALUES prfInputs = {0};
        WEBAUTHN_HMAC_SECRET_SALT globalHmacSalt = {0};
        nsTArray<uint8_t> prfEvalFirst;
        nsTArray<uint8_t> prfEvalSecond;
        nsTArray<nsTArray<uint8_t>> prfEvalByCredIds;
        nsTArray<nsTArray<uint8_t>> prfEvalByCredFirsts;
        nsTArray<bool> prfEvalByCredSecondMaybes;
        nsTArray<nsTArray<uint8_t>> prfEvalByCredSeconds;
        nsTArray<WEBAUTHN_HMAC_SECRET_SALT> hmacSecretSalts;
        nsTArray<WEBAUTHN_CRED_WITH_HMAC_SECRET_SALT>
            credWithHmacSecretSaltList;

        bool requestedPrf;
        (void)aArgs->GetPrf(&requestedPrf);
        if (requestedPrf) {
          rv = aArgs->GetPrfEvalFirst(prfEvalFirst);
          if (rv == NS_OK) {
            globalHmacSalt.cbFirst = prfEvalFirst.Length();
            globalHmacSalt.pbFirst = prfEvalFirst.Elements();
            prfInputs.pGlobalHmacSalt = &globalHmacSalt;
          }
          rv = aArgs->GetPrfEvalSecond(prfEvalSecond);
          if (rv == NS_OK) {
            globalHmacSalt.cbSecond = prfEvalSecond.Length();
            globalHmacSalt.pbSecond = prfEvalSecond.Elements();
          }
          if (NS_OK ==
                  aArgs->GetPrfEvalByCredentialCredentialId(prfEvalByCredIds) &&
              NS_OK ==
                  aArgs->GetPrfEvalByCredentialEvalFirst(prfEvalByCredFirsts) &&
              NS_OK == aArgs->GetPrfEvalByCredentialEvalSecondMaybe(
                           prfEvalByCredSecondMaybes) &&
              NS_OK == aArgs->GetPrfEvalByCredentialEvalSecond(
                           prfEvalByCredSeconds) &&
              prfEvalByCredIds.Length() == prfEvalByCredFirsts.Length() &&
              prfEvalByCredIds.Length() == prfEvalByCredSecondMaybes.Length() &&
              prfEvalByCredIds.Length() == prfEvalByCredSeconds.Length()) {
            for (size_t i = 0; i < prfEvalByCredIds.Length(); i++) {
              WEBAUTHN_HMAC_SECRET_SALT salt = {0};
              salt.cbFirst = prfEvalByCredFirsts[i].Length();
              salt.pbFirst = prfEvalByCredFirsts[i].Elements();
              if (prfEvalByCredSecondMaybes[i]) {
                salt.cbSecond = prfEvalByCredSeconds[i].Length();
                salt.pbSecond = prfEvalByCredSeconds[i].Elements();
              }
              hmacSecretSalts.AppendElement(salt);
            }
            // The credWithHmacSecretSaltList array will contain raw pointers to
            // elements of the hmacSecretSalts array, so we must not cause
            // any re-allocations of hmacSecretSalts from this point.
            for (size_t i = 0; i < prfEvalByCredIds.Length(); i++) {
              WEBAUTHN_CRED_WITH_HMAC_SECRET_SALT value = {0};
              value.cbCredID = prfEvalByCredIds[i].Length();
              value.pbCredID = prfEvalByCredIds[i].Elements();
              value.pHmacSecretSalt = &hmacSecretSalts[i];
              credWithHmacSecretSaltList.AppendElement(value);
            }
            prfInputs.cCredWithHmacSecretSaltList =
                credWithHmacSecretSaltList.Length();
            prfInputs.pCredWithHmacSecretSaltList =
                credWithHmacSecretSaltList.Elements();
          }

          pPrfInputs = &prfInputs;
        }

        // https://w3c.github.io/webauthn/#prf-extension
        // "The hmac-secret extension provides two PRFs per credential: one
        // which is used for requests where user verification is performed and
        // another for all other requests. This extension [PRF] only exposes a
        // single PRF per credential and, when implementing on top of
        // hmac-secret, that PRF MUST be the one used for when user verification
        // is performed. This overrides the UserVerificationRequirement if
        // neccessary."
        if (pPrfInputs &&
            winUserVerificationReq ==
                WEBAUTHN_USER_VERIFICATION_REQUIREMENT_DISCOURAGED) {
          winUserVerificationReq =
              WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED;
        }

        // allow Credentials
        nsTArray<nsTArray<uint8_t>> allowList;
        nsTArray<uint8_t> allowListTransports;
        (void)aArgs->GetAllowList(allowList);
        (void)aArgs->GetAllowListTransports(allowListTransports);

        if (allowList.Length() != allowListTransports.Length()) {
          aPromise->Reject(NS_ERROR_DOM_UNKNOWN_ERR);
          return;
        }

        nsTArray<WEBAUTHN_CREDENTIAL_EX> allowCredentials;
        WEBAUTHN_CREDENTIAL_EX* pAllowCredentials = nullptr;
        nsTArray<WEBAUTHN_CREDENTIAL_EX*> allowCredentialsPtrs;
        WEBAUTHN_CREDENTIAL_LIST allowCredentialList = {0};
        WEBAUTHN_CREDENTIAL_LIST* pAllowCredentialList = nullptr;

        for (size_t i = 0; i < allowList.Length(); i++) {
          nsTArray<uint8_t>& cred = allowList[i];
          uint8_t& transports = allowListTransports[i];
          DWORD winTransports = 0;
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_USB) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_USB;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_NFC) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_NFC;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_BLE) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_BLE;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_INTERNAL) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_INTERNAL;
          }
          if (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_HYBRID) {
            winTransports |= WEBAUTHN_CTAP_TRANSPORT_HYBRID;
          }

          WEBAUTHN_CREDENTIAL_EX credential = {
              WEBAUTHN_CREDENTIAL_EX_CURRENT_VERSION,
              static_cast<DWORD>(cred.Length()), (PBYTE)(cred.Elements()),
              WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY, winTransports};
          allowCredentials.AppendElement(credential);
        }

        if (allowCredentials.Length()) {
          pAllowCredentials = allowCredentials.Elements();
          for (DWORD i = 0; i < allowCredentials.Length(); i++) {
            allowCredentialsPtrs.AppendElement(&pAllowCredentials[i]);
          }
          allowCredentialList.cCredentials = allowCredentials.Length();
          allowCredentialList.ppCredentials = allowCredentialsPtrs.Elements();
          pAllowCredentialList = &allowCredentialList;
        }

        nsTArray<nsString> inputHints;
        (void)aArgs->GetHints(inputHints);

        nsTArray<LPCWSTR> hints;
        PrunePublicKeyCredentialHints(inputHints, hints);

        uint32_t timeout_u32;
        (void)aArgs->GetTimeoutMS(&timeout_u32);
        DWORD timeout = timeout_u32;

        bool privateBrowsing;
        (void)aArgs->GetPrivateBrowsing(&privateBrowsing);
        BOOL winPrivateBrowsing = FALSE;
        if (privateBrowsing) {
          winPrivateBrowsing = TRUE;
        }

        WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS WebAuthNAssertionOptions =
            {
                WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS_VERSION_8,
                timeout,
                {0, NULL},
                {0, NULL},
                winAttachment,
                winUserVerificationReq,
                0,  // dwFlags
                winAppIdentifier,
                pbAppIdUsed,
                &aCancellationId,  // CancellationId
                pAllowCredentialList,
                credLargeBlobOperation,  // CredLargeBlobOperation
                credLargeBlobSize,       // Size of CredLargeBlob
                credLargeBlob,           // CredLargeBlob
                pPrfInputs,              // HmacSecretSaltValues
                winPrivateBrowsing,      // BrowserInPrivateMode
                NULL,                    // LinkedDevice
                FALSE,                   // AutoFill
                0,                       // Size of JsonExt
                NULL,                    // JsonExt
                (DWORD)hints.Length(),   // Size of CredentialHints
                hints.Elements(),        // CredentialHints
            };

        PWEBAUTHN_ASSERTION pWebAuthNAssertion = nullptr;

        // Bug 1518876: Get Window Handle from Content process for Windows
        // WebAuthN APIs
        HWND hWnd = GetForegroundWindow();

        HRESULT hr = gWinWebauthnGetAssertion(
            hWnd, rpId.get(), &WebAuthNClientData, &WebAuthNAssertionOptions,
            &pWebAuthNAssertion);

        if (hr == S_OK) {
          RefPtr<WebAuthnSignResult> result = new WebAuthnSignResult(
              clientDataJSON, credLargeBlobOperation, pWebAuthNAssertion);
          gWinWebauthnFreeAssertion(pWebAuthNAssertion);
          if (winAppIdentifier != nullptr) {
            // The gWinWebauthnGetAssertion call modified bAppIdUsed through
            // a pointer provided in WebAuthNAssertionOptions.
            (void)result->SetUsedAppId(bAppIdUsed == TRUE);
          }
          aPromise->Resolve(result);
        } else {
          PCWSTR errorName = gWinWebauthnGetErrorName(hr);
          nsresult aError = NS_ERROR_DOM_ABORT_ERR;

          if (_wcsicmp(errorName, L"InvalidStateError") == 0) {
            aError = NS_ERROR_DOM_INVALID_STATE_ERR;
          } else if (_wcsicmp(errorName, L"ConstraintError") == 0 ||
                     _wcsicmp(errorName, L"UnknownError") == 0) {
            aError = NS_ERROR_DOM_UNKNOWN_ERR;
          } else if (_wcsicmp(errorName, L"NotSupportedError") == 0) {
            aError = NS_ERROR_DOM_INVALID_STATE_ERR;
          } else if (_wcsicmp(errorName, L"NotAllowedError") == 0) {
            aError = NS_ERROR_DOM_NOT_ALLOWED_ERR;
          }

          aPromise->Reject(aError);
        }
      }));

  NS_DispatchBackgroundTask(runnable, NS_DISPATCH_EVENT_MAY_BLOCK);
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::HasPendingConditionalGet(uint64_t aBrowsingContextId,
                                             const nsAString& aOrigin,
                                             uint64_t* aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  *aRv = 0;
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::GetAutoFillEntries(
    uint64_t aTransactionId, nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  aCallback->Reject(NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::GetAutoFillEntriesForRpId(
    const nsAString& aRpId, const nsTArray<nsTArray<uint8_t>>& aAllowList,
    nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = EnsureWinWebAuthnModuleLoaded();
  if (NS_FAILED(rv)) {
    aCallback->Reject(rv);
    return NS_OK;
  }

  StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
  if (!gWinWebAuthnModule) {
    aCallback->Reject(NS_ERROR_NOT_AVAILABLE);
    return NS_OK;
  }

  nsTArray<RefPtr<nsIWebAuthnAutoFillEntry>> entries;
  if (gWinWebauthnGetApiVersionNumber() < WEBAUTHN_API_VERSION_4) {
    // GetPlatformCredentialList was added in version 4. Earlier versions
    // can still present a generic "Use a Passkey" autofill entry, so we
    // resolve with an empty list rather than rejecting.
    aCallback->Resolve(entries);
    return NS_OK;
  }

  nsString rpId(aRpId);
  WEBAUTHN_GET_CREDENTIALS_OPTIONS getCredentialsOptions{
      WEBAUTHN_GET_CREDENTIALS_OPTIONS_VERSION_1,
      rpId.get(),  // pwszRpId
      FALSE,       // bBrowserInPrivateMode
  };
  PWEBAUTHN_CREDENTIAL_DETAILS_LIST pCredentialList = nullptr;
  HRESULT hr = gWinWebauthnGetPlatformCredentialList(&getCredentialsOptions,
                                                     &pCredentialList);
  // WebAuthNGetPlatformCredentialList has an _Outptr_result_maybenull_
  // annotation and a comment "Returns NTE_NOT_FOUND when credentials are
  // not found."
  if (pCredentialList == nullptr) {
    if (hr != NTE_NOT_FOUND) {
      aCallback->Reject(NS_ERROR_FAILURE);
      return NS_OK;
    }
  } else {
    MOZ_ASSERT(hr == S_OK);
    for (size_t i = 0; i < pCredentialList->cCredentialDetails; i++) {
      entries.AppendElement(
          new WebAuthnAutoFillEntry(pCredentialList->ppCredentialDetails[i]));
    }
    gWinWebauthnFreePlatformCredentialList(pCredentialList);
  }
  aCallback->Resolve(entries);
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::SelectAutoFillEntry(
    uint64_t aTransactionId, const nsTArray<uint8_t>& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WinWebAuthnService::ResumeConditionalGet(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WinWebAuthnService::PinCallback(uint64_t aTransactionId,
                                const nsACString& aPin) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::SetHasAttestationConsent(uint64_t aTransactionId,
                                             bool aHasConsent) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::SelectionCallback(uint64_t aTransactionId,
                                      uint64_t aIndex) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::AddVirtualAuthenticator(
    const nsACString& aProtocol, const nsACString& aTransport,
    bool aHasResidentKey, bool aHasUserVerification, bool aIsUserConsenting,
    bool aIsUserVerified, nsACString& aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::HasVirtualAuthenticator(const nsACString& aAuthenticatorId,
                                            bool* aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::RemoveVirtualAuthenticator(
    const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::AddCredential(const nsACString& aAuthenticatorId,
                                  const nsACString& aCredentialId,
                                  bool aIsResidentCredential,
                                  const nsACString& aRpId,
                                  const nsACString& aPrivateKey,
                                  const nsACString& aUserHandle,
                                  uint32_t aSignCount) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::GetCredentials(
    const nsACString& aAuthenticatorId,
    nsTArray<RefPtr<nsICredentialParameters>>& _aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::RemoveCredential(const nsACString& aAuthenticatorId,
                                     const nsACString& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::RemoveAllCredentials(const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::SetUserVerified(const nsACString& aAuthenticatorId,
                                    bool aIsUserVerified) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::Listen() {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::RunCommand(const nsACString& aCmd) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::StartLogging() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
WinWebAuthnService::StopLogging() { return NS_ERROR_NOT_IMPLEMENTED; }

}  // namespace mozilla::dom
