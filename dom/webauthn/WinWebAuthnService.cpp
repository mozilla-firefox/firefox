/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/PWebAuthnTransactionParent.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/Assertions.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/Unused.h"

#include "nsTextFormatter.h"
#include "nsWindowsHelpers.h"
#include "WebAuthnAutoFillEntry.h"
#include "WebAuthnEnumStrings.h"
#include "WebAuthnResult.h"
#include "WebAuthnTransportIdentifiers.h"
#include "winwebauthn/webauthn.h"
#include "WinWebAuthnService.h"

namespace mozilla::dom {

namespace {
StaticRWLock gWinWebAuthnModuleLock;

static bool gWinWebAuthnModuleUnusable = false;
static HMODULE gWinWebAuthnModule = 0;

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
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::Reset() {
  // Reset will never be the first function to use gWinWebAuthnModule, so
  // we shouldn't try to initialize it here.
  auto guard = mTransactionState.Lock();
  if (guard->isSome()) {
    StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
    if (gWinWebAuthnModule) {
      const GUID cancellationId = guard->ref().cancellationId;
      gWinWebauthnCancelCurrentOperation(&cancellationId);
    }
    if (guard->ref().pendingSignPromise.isSome()) {
      // This request was never dispatched to the platform API, so
      // we need to reject the promise ourselves.
      guard->ref().pendingSignPromise.ref()->Reject(
          NS_ERROR_DOM_NOT_ALLOWED_ERR);
    }
    guard->reset();
  }

  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::MakeCredential(uint64_t aTransactionId,
                                   uint64_t aBrowsingContextId,
                                   nsIWebAuthnRegisterArgs* aArgs,
                                   nsIWebAuthnRegisterPromise* aPromise) {
  nsresult rv = EnsureWinWebAuthnModuleLoaded();
  NS_ENSURE_SUCCESS(rv, rv);

  Reset();
  auto guard = mTransactionState.Lock();
  StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
  GUID cancellationId;
  if (gWinWebauthnGetCancellationId(&cancellationId) != S_OK) {
    // caller will reject promise
    return NS_ERROR_DOM_UNKNOWN_ERR;
  }
  *guard = Some(TransactionState{
      aTransactionId,
      aBrowsingContextId,
      Nothing(),
      Nothing(),
      cancellationId,
  });

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
        Unused << aArgs->GetRpId(rpId);
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
        Unused << aArgs->GetClientDataJSON(clientDataJSON);
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
        Unused << aArgs->GetRpName(rpName);
        rpInfo.pwszName = rpName.get();
        rpInfo.pwszIcon = nullptr;

        nsTArray<uint8_t> userId;
        Unused << aArgs->GetUserId(userId);
        userInfo.cbId = static_cast<DWORD>(userId.Length());
        userInfo.pbId = const_cast<unsigned char*>(userId.Elements());

        nsString userName;
        Unused << aArgs->GetUserName(userName);
        userInfo.pwszName = userName.get();

        userInfo.pwszIcon = nullptr;

        nsString userDisplayName;
        Unused << aArgs->GetUserDisplayName(userDisplayName);
        userInfo.pwszDisplayName = userDisplayName.get();

        // Algorithms
        nsTArray<WEBAUTHN_COSE_CREDENTIAL_PARAMETER> coseParams;
        nsTArray<int32_t> coseAlgs;
        Unused << aArgs->GetCoseAlgs(coseAlgs);
        for (const int32_t& coseAlg : coseAlgs) {
          WEBAUTHN_COSE_CREDENTIAL_PARAMETER coseAlgorithm = {
              WEBAUTHN_COSE_CREDENTIAL_PARAMETER_CURRENT_VERSION,
              WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY, coseAlg};
          coseParams.AppendElement(coseAlgorithm);
        }

        nsString userVerificationReq;
        Unused << aArgs->GetUserVerification(userVerificationReq);
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
        Unused << aArgs->GetResidentKey(residentKey);
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
        Unused << aArgs->GetAttestationConveyancePreference(attestation);
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
        Unused << aArgs->GetExcludeList(excludeList);

        nsTArray<uint8_t> excludeListTransports;
        Unused << aArgs->GetExcludeListTransports(excludeListTransports);

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
        Unused << aArgs->GetTimeoutMS(&timeout_u32);
        DWORD timeout = timeout_u32;

        bool privateBrowsing;
        Unused << aArgs->GetPrivateBrowsing(&privateBrowsing);
        BOOL winPrivateBrowsing = FALSE;
        if (privateBrowsing) {
          winPrivateBrowsing = TRUE;
        }

        // MakeCredentialOptions
        WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS
        WebAuthNCredentialOptions = {
            WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_7,
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
            largeBlobSupport,      // LargeBlobSupport
            winPreferResidentKey,  // PreferResidentKey
            winPrivateBrowsing,    // BrowserInPrivateMode
            winEnablePrf,          // EnablePrf
            NULL,                  // LinkedDevice
            0,                     // size of JsonExt
            NULL,                  // JsonExt
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
          Unused << aArgs->GetCredProps(&requestedCredProps);
          if (requestedCredProps &&
              pWebAuthNCredentialAttestation->dwVersion >=
                  WEBAUTHN_CREDENTIAL_ATTESTATION_VERSION_4) {
            BOOL rk = pWebAuthNCredentialAttestation->bResidentKey;
            Unused << result->SetCredPropsRk(rk == TRUE);
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
  nsresult rv = EnsureWinWebAuthnModuleLoaded();
  NS_ENSURE_SUCCESS(rv, rv);

  Reset();

  auto guard = mTransactionState.Lock();

  GUID cancellationId;
  {
    StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
    if (gWinWebauthnGetCancellationId(&cancellationId) != S_OK) {
      // caller will reject promise
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }
  }

  *guard = Some(TransactionState{
      aTransactionId,
      aBrowsingContextId,
      Some(RefPtr{aArgs}),
      Some(RefPtr{aPromise}),
      cancellationId,
  });

  bool conditionallyMediated;
  Unused << aArgs->GetConditionallyMediated(&conditionallyMediated);
  if (!conditionallyMediated) {
    DoGetAssertion(Nothing(), guard);
  }
  return NS_OK;
}

void WinWebAuthnService::DoGetAssertion(
    Maybe<nsTArray<uint8_t>>&& aSelectedCredentialId,
    const TransactionStateMutex::AutoLock& aGuard) {
  if (aGuard->isNothing() || aGuard->ref().pendingSignArgs.isNothing() ||
      aGuard->ref().pendingSignPromise.isNothing()) {
    return;
  }

  // Take the pending Args and Promise to prevent repeated calls to
  // DoGetAssertion for this transaction.
  RefPtr<nsIWebAuthnSignArgs> aArgs = aGuard->ref().pendingSignArgs.extract();
  RefPtr<nsIWebAuthnSignPromise> aPromise =
      aGuard->ref().pendingSignPromise.extract();

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "WinWebAuthnService::MakeCredential",
      [self = RefPtr{this}, aArgs, aPromise,
       aSelectedCredentialId = std::move(aSelectedCredentialId),
       aCancellationId = aGuard->ref().cancellationId]() mutable {
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
        Unused << aArgs->GetClientDataJSON(clientDataJSON);
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
        Unused << aArgs->GetRpId(rpId);

        // User Verification Requirement
        nsString userVerificationReq;
        Unused << aArgs->GetUserVerification(userVerificationReq);
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
        Unused << aArgs->GetPrf(&requestedPrf);
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
        if (aSelectedCredentialId.isSome()) {
          allowList.AppendElement(aSelectedCredentialId.extract());
          allowListTransports.AppendElement(
              MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_INTERNAL);
        } else {
          Unused << aArgs->GetAllowList(allowList);
          Unused << aArgs->GetAllowListTransports(allowListTransports);
        }

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

        uint32_t timeout_u32;
        Unused << aArgs->GetTimeoutMS(&timeout_u32);
        DWORD timeout = timeout_u32;

        bool privateBrowsing;
        Unused << aArgs->GetPrivateBrowsing(&privateBrowsing);
        BOOL winPrivateBrowsing = FALSE;
        if (privateBrowsing) {
          winPrivateBrowsing = TRUE;
        }

        WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS WebAuthNAssertionOptions =
            {
                WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS_VERSION_7,
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
            Unused << result->SetUsedAppId(bAppIdUsed == TRUE);
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
}

NS_IMETHODIMP
WinWebAuthnService::HasPendingConditionalGet(uint64_t aBrowsingContextId,
                                             const nsAString& aOrigin,
                                             uint64_t* aRv) {
  auto guard = mTransactionState.Lock();
  if (guard->isNothing() ||
      guard->ref().browsingContextId != aBrowsingContextId ||
      guard->ref().pendingSignArgs.isNothing()) {
    *aRv = 0;
    return NS_OK;
  }

  nsString origin;
  Unused << guard->ref().pendingSignArgs.ref()->GetOrigin(origin);
  if (origin != aOrigin) {
    *aRv = 0;
    return NS_OK;
  }

  *aRv = guard->ref().transactionId;
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::GetAutoFillEntries(
    uint64_t aTransactionId, nsTArray<RefPtr<nsIWebAuthnAutoFillEntry>>& aRv) {
  aRv.Clear();
  nsString rpId;

  {
    auto guard = mTransactionState.Lock();
    if (guard->isNothing() || guard->ref().transactionId != aTransactionId ||
        guard->ref().pendingSignArgs.isNothing()) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    Unused << guard->ref().pendingSignArgs.ref()->GetRpId(rpId);
  }

  StaticAutoReadLock moduleLock(gWinWebAuthnModuleLock);
  if (!gWinWebAuthnModule) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (gWinWebauthnGetApiVersionNumber() < WEBAUTHN_API_VERSION_4) {
    // GetPlatformCredentialList was added in version 4. Earlier versions
    // can still present a generic "Use a Passkey" autofill entry, so
    // this isn't an error.
    return NS_OK;
  }

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
      return NS_ERROR_FAILURE;
    }
    return NS_OK;
  }
  MOZ_ASSERT(hr == S_OK);
  for (size_t i = 0; i < pCredentialList->cCredentialDetails; i++) {
    RefPtr<nsIWebAuthnAutoFillEntry> entry(
        new WebAuthnAutoFillEntry(pCredentialList->ppCredentialDetails[i]));
    aRv.AppendElement(entry);
  }
  gWinWebauthnFreePlatformCredentialList(pCredentialList);
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::SelectAutoFillEntry(
    uint64_t aTransactionId, const nsTArray<uint8_t>& aCredentialId) {
  auto guard = mTransactionState.Lock();
  if (guard->isNothing() || guard->ref().transactionId != aTransactionId ||
      guard->ref().pendingSignArgs.isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsTArray<nsTArray<uint8_t>> allowList;
  Unused << guard->ref().pendingSignArgs.ref()->GetAllowList(allowList);
  if (!allowList.IsEmpty() && !allowList.Contains(aCredentialId)) {
    return NS_ERROR_FAILURE;
  }

  Maybe<nsTArray<uint8_t>> id;
  id.emplace();
  id.ref().Assign(aCredentialId);
  DoGetAssertion(std::move(id), guard);

  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::ResumeConditionalGet(uint64_t aTransactionId) {
  auto guard = mTransactionState.Lock();
  if (guard->isNothing() || guard->ref().transactionId != aTransactionId ||
      guard->ref().pendingSignArgs.isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  DoGetAssertion(Nothing(), guard);
  return NS_OK;
}

NS_IMETHODIMP
WinWebAuthnService::PinCallback(uint64_t aTransactionId,
                                const nsACString& aPin) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::SetHasAttestationConsent(uint64_t aTransactionId,
                                             bool aHasConsent) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::SelectionCallback(uint64_t aTransactionId,
                                      uint64_t aIndex) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::AddVirtualAuthenticator(
    const nsACString& protocol, const nsACString& transport,
    bool hasResidentKey, bool hasUserVerification, bool isUserConsenting,
    bool isUserVerified, uint64_t* _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::RemoveVirtualAuthenticator(uint64_t authenticatorId) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::AddCredential(uint64_t authenticatorId,
                                  const nsACString& credentialId,
                                  bool isResidentCredential,
                                  const nsACString& rpId,
                                  const nsACString& privateKey,
                                  const nsACString& userHandle,
                                  uint32_t signCount) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::GetCredentials(
    uint64_t authenticatorId,
    nsTArray<RefPtr<nsICredentialParameters>>& _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::RemoveCredential(uint64_t authenticatorId,
                                     const nsACString& credentialId) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::RemoveAllCredentials(uint64_t authenticatorId) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::SetUserVerified(uint64_t authenticatorId,
                                    bool isUserVerified) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WinWebAuthnService::Listen() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
WinWebAuthnService::RunCommand(const nsACString& cmd) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

}  // namespace mozilla::dom
