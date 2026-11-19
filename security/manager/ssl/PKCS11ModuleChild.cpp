/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if defined(MOZ_NO_SMART_CARDS)
#  error This file should only be used when MOZ_NO_SMART_CARDS is not defined.
#endif  // MOZ_NO_SMART_CARDS

#include "mozilla/psm/PKCS11ModuleChild.h"

#include "NSSCertDBTrustDomain.h"
#include "PKCS11ModuleDB.h"
#include "PKCS11Token.h"
#include "ScopedNSSTypes.h"
#include "certdb.h"
#include "mozilla/Logging.h"
#include "mozilla/ipc/Endpoint.h"
#include "nsDebugImpl.h"
#include "nss.h"

namespace mozilla::psm {

static LazyLogModule gRemotePKCS11Log("remotepkcs11");

char* RemotePKCS11PasswordPrompt(PK11SlotInfo* slot, PRBool _retry, void* ctx) {
  MOZ_ASSERT(ctx);
  if (!ctx) {
    return nullptr;
  }
  PKCS11ModuleChild* pkcs11ModuleChild(static_cast<PKCS11ModuleChild*>(ctx));
  if (PK11_ProtectedAuthenticationPath(slot)) {
    return pkcs11ModuleChild->InitiateProtectedAuth(slot);
  }
  return pkcs11ModuleChild->PromptForPassword(slot);
}

void ConfigureNSSInPKCS11UtilityProcess(const nsACString& profilePath) {
  MOZ_LOG(gRemotePKCS11Log, LogLevel::Debug,
          ("ConfigureNSSInPKCS11UtilityProcess: profilePath: '%s'",
           PromiseFlatCString(profilePath).get()));
  NSSDBConfig readWriteConfig = NSSDBConfig::ReadWrite;
  // If NSS is initialized in read-write mode with an empty path, it will
  // create a `pkcs11.txt` file in the current working directory, which is not
  // the desired behavior.
  if (profilePath.IsEmpty()) {
    MOZ_LOG(gRemotePKCS11Log, LogLevel::Debug,
            ("profilePath is empty: initializing in read-only mode"));
    readWriteConfig = NSSDBConfig::ReadOnly;
  }
  if (InitializeNSS(profilePath, readWriteConfig,
                    PKCS11DBConfig::LoadModules) != SECSuccess) {
    MOZ_LOG(gRemotePKCS11Log, LogLevel::Error,
            ("Failed to initialize NSS: %d", PR_GetError()));
    return;
  }
  PK11_SetPasswordFunc(RemotePKCS11PasswordPrompt);
}

nsresult PKCS11ModuleChild::Start(Endpoint<PPKCS11ModuleChild>&& aEndpoint,
                                  nsCString&& aProfilePath) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mTaskQueue);
  MOZ_ASSERT(!mAuthTaskQueue);

  nsDebugImpl::SetMultiprocessMode("PKCS11ModuleChild");

  nsresult rv = NS_CreateBackgroundTaskQueue("PKCS11ModuleChild::IPC",
                                             getter_AddRefs(mTaskQueue));
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = NS_CreateBackgroundTaskQueue("PKCS11ModuleChild::Auth",
                                    getter_AddRefs(mAuthTaskQueue));
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = mTaskQueue->Dispatch(NS_NewRunnableFunction(
      "PKCS11ModuleChild::StartBind",
      [self = RefPtr{this}, endpoint = std::move(aEndpoint),
       profilePath = std::move(aProfilePath)]() mutable {
        ConfigureNSSInPKCS11UtilityProcess(profilePath);
        MOZ_ALWAYS_TRUE(endpoint.Bind(self));
      }));
  return rv;
}

ipc::IPCResult PKCS11ModuleChild::RecvAddModule(nsCString&& aModuleName,
                                                nsCString&& aLibraryPath,
                                                uint32_t aMechanismFlags,
                                                uint32_t aCipherFlags,
                                                AddModuleResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(NS_ERROR_NOT_AVAILABLE);
    return IPC_OK();
  }
  aResolver(PKCS11ModuleDB::DoAddModule(aModuleName, aLibraryPath,
                                        aMechanismFlags, aCipherFlags));
  return IPC_OK();
}

ipc::IPCResult PKCS11ModuleChild::RecvDeleteModule(
    nsCString&& aModuleName, DeleteModuleResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(NS_ERROR_NOT_AVAILABLE);
    return IPC_OK();
  }
  aResolver(PKCS11ModuleDB::DoDeleteModule(aModuleName));
  return IPC_OK();
}

ipc::IPCResult PKCS11ModuleChild::RecvListModules(
    ListModulesResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(std::make_tuple(NS_ERROR_NOT_AVAILABLE, nsTArray<ModuleInfo>{}));
    return IPC_OK();
  }
  nsTArray<ModuleInfo> modules;
  nsresult rv = PKCS11ModuleDB::DoListModules(modules);
  MOZ_LOG(gRemotePKCS11Log, LogLevel::Debug,
          ("RecvListModules returning %zu modules", modules.Length()));
  aResolver(std::make_tuple(rv, std::move(modules)));
  return IPC_OK();
}

template <typename Operation>
nsresult DoWithToken(SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
                     TokenInfo& aTokenInfo, Operation&& operation) {
  UniquePK11SlotInfo slot(SECMOD_LookupSlot(aModuleID, aSlotID));
  if (!slot) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = operation(slot.get());
  if (NS_FAILED(rv)) {
    return rv;
  }
  RefPtr<PKCS11Token> token(MakeAndAddRef<PKCS11Token>(slot.get()));
  return token->GetTokenInfo(aTokenInfo);
}

ipc::IPCResult PKCS11ModuleChild::RecvResetToken(
    SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
    ResetTokenResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(std::make_tuple(NS_ERROR_NOT_AVAILABLE, TokenInfo{}));
    return IPC_OK();
  }
  TokenInfo tokenInfo;
  nsresult rv =
      DoWithToken(aModuleID, aSlotID, tokenInfo, [](PK11SlotInfo* slot) {
        SECStatus rv = PK11_ResetToken(slot, nullptr);
        if (rv != SECSuccess) {
          return MapSECStatus(rv);
        }
        return NS_OK;
      });
  aResolver(std::make_tuple(rv, std::move(tokenInfo)));
  return IPC_OK();
}

ipc::IPCResult PKCS11ModuleChild::RecvLoginToken(
    SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
    LoginTokenResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(std::make_pair(NS_ERROR_NOT_AVAILABLE, TokenInfo{}));
    return IPC_OK();
  }
  mAuthTaskQueue->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, moduleID(aModuleID), slotID(aSlotID),
                 resolver(std::move(aResolver))] {
        TokenInfo tokenInfo;
        nsresult rv = DoWithToken(
            moduleID, slotID, tokenInfo, [self](PK11SlotInfo* slot) {
              SECStatus rv = PK11_Authenticate(slot, true, self.get());
              if (rv != SECSuccess) {
                return MapSECStatus(rv);
              }
              return NS_OK;
            });
        self->mTaskQueue->Dispatch(NS_NewRunnableFunction(
            __func__, [rv, tokenInfo(std::move(tokenInfo)),
                       resolver(std::move(resolver))] {
              resolver(std::make_pair(rv, std::move(tokenInfo)));
            }));
      }));
  return IPC_OK();
}

ipc::IPCResult PKCS11ModuleChild::RecvLogoutToken(
    SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
    LogoutTokenResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(std::make_pair(NS_ERROR_NOT_AVAILABLE, TokenInfo{}));
    return IPC_OK();
  }
  TokenInfo tokenInfo;
  nsresult rv =
      DoWithToken(aModuleID, aSlotID, tokenInfo, [](PK11SlotInfo* slot) {
        // PK11_Logout() can fail if the user wasn't logged in beforehand. We
        // want this method to succeed even in this case, so we ignore the
        // return value.
        (void)PK11_Logout(slot);
        return NS_OK;
      });
  aResolver(std::make_tuple(rv, std::move(tokenInfo)));
  return IPC_OK();
}

nsresult DoChangeTokenPassword(SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
                               const nsCString& aOldPassword,
                               const nsCString& aNewPassword,
                               TokenInfo& aTokenInfo) {
  UniquePK11SlotInfo slot(SECMOD_LookupSlot(aModuleID, aSlotID));
  if (!slot) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = DoChangePassword(slot, aOldPassword, aNewPassword);
  if (NS_FAILED(rv)) {
    return rv;
  }
  RefPtr<PKCS11Token> token(MakeAndAddRef<PKCS11Token>(slot.get()));
  return token->GetTokenInfo(aTokenInfo);
}

ipc::IPCResult PKCS11ModuleChild::RecvChangeTokenPassword(
    SECMODModuleID aModuleID, CK_SLOT_ID aSlotID, const nsCString& aOldPassword,
    const nsCString& aNewPassword, ChangeTokenPasswordResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(std::make_pair(NS_ERROR_NOT_AVAILABLE, TokenInfo{}));
    return IPC_OK();
  }
  TokenInfo tokenInfo;
  nsresult rv = DoChangeTokenPassword(aModuleID, aSlotID, aOldPassword,
                                      aNewPassword, tokenInfo);
  aResolver(std::make_tuple(rv, std::move(tokenInfo)));
  return IPC_OK();
}

char* PKCS11ModuleChild::PromptForPassword(PK11SlotInfo* slot) {
  MonitorAutoLock authPromptMonitorLock(mAuthPromptMonitor);
  mMaybePasswordForPrompt.reset();
  mMaybeProtectedAuthPrompt.reset();

  MOZ_ASSERT(mAuthTaskQueue->IsOnCurrentThread());
  if (!mAuthTaskQueue->IsOnCurrentThread()) {
    return nullptr;
  }

  nsCString tokenName(PK11_GetTokenName(slot));
  mTaskQueue->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, tokenName(std::move(tokenName))] {
        self->SendPromptPassword(tokenName)->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self](const PPKCS11ModuleChild::PromptPasswordPromise::
                       ResolveOrRejectValue& value) {
              MonitorAutoLock authPromptMonitorLock(self->mAuthPromptMonitor);
              if (value.IsResolve()) {
                self->mMaybePasswordForPrompt.emplace(
                    std::move(value.ResolveValue()));
              } else {
                self->mMaybePasswordForPrompt.emplace(
                    std::make_tuple(NS_ERROR_FAILURE, ""_ns));
              }
              authPromptMonitorLock.Notify();
            });
      }));

  while (mMaybePasswordForPrompt.isNothing()) {
    authPromptMonitorLock.Wait();
  }
  auto passwordPromptResult(mMaybePasswordForPrompt.take());
  MOZ_ASSERT(passwordPromptResult.isSome());
  if (passwordPromptResult.isNothing()) {
    return nullptr;
  }
  if (NS_FAILED(std::get<0>(*passwordPromptResult))) {
    return nullptr;
  }
  return ToNewCString(std::get<1>(*passwordPromptResult));
}

char* PKCS11ModuleChild::InitiateProtectedAuth(PK11SlotInfo* slot) {
  static uint64_t id = 0;

  MonitorAutoLock authPromptMonitorLock(mAuthPromptMonitor);
  mMaybePasswordForPrompt.reset();
  id++;
  mMaybeProtectedAuthPrompt.emplace(
      std::make_pair(ProtectedAuthState::InProgress, id));

  MOZ_ASSERT(mAuthTaskQueue->IsOnCurrentThread());
  if (!mAuthTaskQueue->IsOnCurrentThread()) {
    return nullptr;
  }

  // Dispatch a background task to call C_Login. The call will block until the
  // protected authentication (e.g. card reader PIN entry) succeeds or fails.
  nsresult rv = NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          __func__,
          [self = RefPtr{this},
           slot = UniquePK11SlotInfo(PK11_ReferenceSlot(slot)), id = id]() {
            SECStatus rv = PK11_CheckUserPassword(slot.get(), nullptr);
            ProtectedAuthState newState = ProtectedAuthState::Cancelled;
            switch (rv) {
              case SECSuccess:
                newState = ProtectedAuthState::Succeeded;
                break;
              case SECWouldBlock:
                newState = ProtectedAuthState::DoRetry;
                break;
              default:
                break;
            }
            // Notify the thread waiting on this authentication that it has
            // completed.
            MonitorAutoLock authPromptMonitorLock(self->mAuthPromptMonitor);
            if (self->mMaybeProtectedAuthPrompt.isSome() &&
                std::get<1>(*self->mMaybeProtectedAuthPrompt) == id) {
              self->mMaybeProtectedAuthPrompt =
                  Some(std::make_pair(newState, id));
              authPromptMonitorLock.Notify();
            }
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  // Meanwhile, tell the parent process to show an indication that a protected
  // auth attempt is in progress.
  nsCString tokenName(PK11_GetTokenName(slot));
  mTaskQueue->Dispatch(NS_NewRunnableFunction(
      __func__,
      [self = RefPtr{this}, tokenName(std::move(tokenName)), id = id] {
        self->SendShowProtectedAuthPrompt(tokenName, id);
      }));

  // Wait for either the background call to C_Login to complete or the parent
  // to tell this thread to stop waiting (indicating a cancel). In the latter
  // case, the background call to C_Login will continue to block until
  // presumably the token times out the operation.
  while (mMaybeProtectedAuthPrompt.isSome() &&
         std::get<0>(*mMaybeProtectedAuthPrompt) ==
             ProtectedAuthState::InProgress) {
    authPromptMonitorLock.Wait();
  }

  // Tell the parent process to discard any remaining protected auth indicators
  // now that this attempt has finished.
  mTaskQueue->Dispatch(
      NS_NewRunnableFunction(__func__, [self = RefPtr{this}, id = id] {
        self->SendDismissProtectedAuthPrompt(id);
      }));

  auto protectedAuthPrompt(mMaybeProtectedAuthPrompt.take());
  MOZ_ASSERT(protectedAuthPrompt.isSome() &&
             std::get<1>(*protectedAuthPrompt) == id);
  if (protectedAuthPrompt.isNothing() ||
      std::get<1>(*protectedAuthPrompt) != id) {
    return nullptr;
  }
  switch (std::get<0>(*protectedAuthPrompt)) {
    case ProtectedAuthState::Succeeded:
      return strdup(PK11_PW_AUTHENTICATED);
    case ProtectedAuthState::DoRetry:
      return strdup(PK11_PW_RETRY);
    default:
      return nullptr;
  }
}

ipc::IPCResult PKCS11ModuleChild::RecvCancelProtectedAuth(uint64_t id) {
  MonitorAutoLock authPromptMonitorLock(mAuthPromptMonitor);
  // If the current in-progress protected auth prompt matches the ID, indicate
  // that it should be cancelled.
  if (mMaybeProtectedAuthPrompt.isSome() &&
      std::get<1>(*mMaybeProtectedAuthPrompt) == id) {
    mMaybeProtectedAuthPrompt =
        Some(std::make_pair(ProtectedAuthState::Cancelled, id));
    authPromptMonitorLock.Notify();
  }
  return IPC_OK();
}

enum class Trust {
  Unknown,
  Anchor,
  Distrusted,
};

Trust GetTrustForCert(CERTCertificate* cert) {
  CERTCertTrust trust;
  // CERT_GetCertTrust returns SECSuccess iff there is a trust record. If there
  // isn't a trust record, trust is unknown.
  if (CERT_GetCertTrust(cert, &trust) != SECSuccess) {
    return Trust::Unknown;
  }
  uint32_t flags = SEC_GET_TRUST_FLAGS(&trust, trustSSL);
  // If the terminal bit is set and the trust bit is not set, this certificate
  // is actively distrusted.
  if ((flags & (CERTDB_TRUSTED_CA | CERTDB_TERMINAL_RECORD)) ==
      CERTDB_TERMINAL_RECORD) {
    return Trust::Distrusted;
  }
  // If the trust bit is set, this is a trust anchor.
  if (flags & CERTDB_TRUSTED_CA) {
    return Trust::Anchor;
  }
  // Otherwise, trust is unknown.
  return Trust::Unknown;
}

ipc::IPCResult PKCS11ModuleChild::RecvFindObjects(
    SearchingFor aSearchingFor, FindObjectsResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(nsTArray<CryptokiObject>{});
    return IPC_OK();
  }
  mAuthTaskQueue->Dispatch(NS_NewRunnableFunction(
      __func__, [searchingFor(aSearchingFor), self = RefPtr{this},
                 resolver(std::move(aResolver))] {
        nsTArray<CryptokiObject> objects;
        PK11CertListType certListType;
        switch (searchingFor) {
          case SearchingFor::ClientCertificates:
            // Using `PK11CertListUserUnique` would make the most sense
            // here, but that turns out to be too conservative in terms of
            // what NSS considers a "client certificate" to be. Instead
            // just search for all certificates.
            certListType = PK11CertListUnique;
            break;
          case SearchingFor::CACertificates:
            certListType = PK11CertListCAUnique;
            break;
        }
        UniqueCERTCertList nssCertificates(
            PK11_ListCerts(certListType, self.get()));
        if (nssCertificates.get()) {
          for (CERTCertListNode* node = CERT_LIST_HEAD(nssCertificates.get());
               !CERT_LIST_END(node, nssCertificates.get());
               node = CERT_LIST_NEXT(node)) {
            Trust trust = GetTrustForCert(node->cert);
            // Don't expose actively distrusted certificates.
            if (trust == Trust::Distrusted) {
              continue;
            }
            Certificate certificate;
            certificate.der().AppendElements(node->cert->derCert.data,
                                             node->cert->derCert.len);
            certificate.serverAuthTrustAnchor() = trust == Trust::Anchor;
            objects.AppendElement(std::move(certificate));

            // If not searching for client certificates, don't look for a
            // corresponding private key.
            if (searchingFor != SearchingFor::ClientCertificates) {
              continue;
            }
            // If a certificate has a corresponding private key, expose the
            // corresponding public key.
            UniqueSECKEYPrivateKey privateKey(
                PK11_FindKeyByAnyCert(node->cert, self.get()));
            if (!privateKey) {
              continue;
            }
            UniqueSECKEYPublicKey publicKey(CERT_ExtractPublicKey(node->cert));
            if (!publicKey) {
              continue;
            }
            nsTArray<uint8_t> certDER(node->cert->derCert.data,
                                      node->cert->derCert.len);
            switch (SECKEY_GetPublicKeyType(publicKey.get())) {
              case rsaKey:
              case rsaPssKey: {
                nsTArray<uint8_t> modulus(publicKey->u.rsa.modulus.data,
                                          publicKey->u.rsa.modulus.len);
                RSAKey rsakey(modulus, certDER);
                objects.AppendElement(std::move(rsakey));
                break;
              }
              case ecKey: {
                nsTArray<uint8_t> params(publicKey->u.ec.DEREncodedParams.data,
                                         publicKey->u.ec.DEREncodedParams.len);
                ECKey eckey(params, certDER);
                objects.AppendElement(std::move(eckey));
                break;
              }
              default:
                break;
            }
          }
        }
        self->mTaskQueue->Dispatch(NS_NewRunnableFunction(
            __func__,
            [objects(std::move(objects)), resolver(std::move(resolver))] {
              MOZ_LOG(
                  gRemotePKCS11Log, LogLevel::Debug,
                  ("RecvFindObjects returning %zu objects", objects.Length()));
              resolver(std::move(objects));
            }));
      }));
  return IPC_OK();
}

static void SignDataGivenCertificate(const nsTArray<uint8_t>& certificate,
                                     const nsTArray<uint8_t>& data,
                                     const nsTArray<uint8_t>& params,
                                     nsTArray<uint8_t>& signature, void* ctx) {
  signature.Clear();
  SECItem certItem = {siBuffer, const_cast<uint8_t*>(certificate.Elements()),
                      static_cast<unsigned int>(certificate.Length())};
  UniqueCERTCertificate cert(CERT_NewTempCertificate(
      CERT_GetDefaultCertDB(), &certItem, nullptr, false, true));
  if (!cert) {
    return;
  }
  UniqueSECKEYPrivateKey key(PK11_FindKeyByAnyCert(cert.get(), ctx));
  if (!key) {
    return;
  }
  SECItem hashItem = {siBuffer, const_cast<uint8_t*>(data.Elements()),
                      static_cast<unsigned int>(data.Length())};
  SECItem paramsItem = {siBuffer, const_cast<uint8_t*>(params.Elements()),
                        static_cast<unsigned int>(params.Length())};
  SECItem* paramsPtr = params.Length() > 0 ? &paramsItem : nullptr;
  CK_MECHANISM_TYPE mechanism;
  switch (key->keyType) {
    case ecKey:
      mechanism = CKM_ECDSA;
      break;
    case rsaKey:
      // If we have params, this is RSA-PSS.
      if (params.Length() > 0) {
        mechanism = CKM_RSA_PKCS_PSS;
      } else {
        // If this is a DigestInfo or a TLS 1.0 MD5/SHA1 hash, this is RSA-PKCS.
        // Otherwise, this is an emsa-pss-encoded digest that should be signed
        // with raw RSA.
        UniqueSGNDigestInfo digestInfo(SGN_DecodeDigestInfo(&hashItem));
        if (digestInfo || data.Length() == 36) {
          mechanism = CKM_RSA_PKCS;
        } else {
          mechanism = CKM_RSA_X_509;
        }
      }
      break;
    default:
      return;
  }
  uint32_t len = PK11_SignatureLen(key.get());
  UniqueSECItem sig(::SECITEM_AllocItem(nullptr, nullptr, len));
  SECStatus srv = PK11_SignWithMechanism(key.get(), mechanism, paramsPtr,
                                         sig.get(), &hashItem);
  if (srv != SECSuccess) {
    return;
  }
  signature.AppendElements(sig->data, sig->len);
}

ipc::IPCResult PKCS11ModuleChild::RecvSign(nsTArray<uint8_t> aCertificate,
                                           nsTArray<uint8_t> aData,
                                           nsTArray<uint8_t> aParams,
                                           SignResolver&& aResolver) {
  if (!NSS_IsInitialized()) {
    aResolver(nsTArray<uint8_t>{});
    return IPC_OK();
  }
  mAuthTaskQueue->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, certificate(std::move(aCertificate)),
                 data(std::move(aData)), params(std::move(aParams)),
                 resolver(std::move(aResolver))] {
        nsTArray<uint8_t> signature;
        SignDataGivenCertificate(certificate, data, params, signature, self);
        self->mTaskQueue->Dispatch(NS_NewRunnableFunction(
            __func__,
            [signature(std::move(signature)), resolver(std::move(resolver))] {
              MOZ_LOG(gRemotePKCS11Log, LogLevel::Debug,
                      ("RecvSign returning signature of length %zu",
                       signature.Length()));
              resolver(std::move(signature));
            }));
      }));
  return IPC_OK();
}

}  // namespace mozilla::psm
