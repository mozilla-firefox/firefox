/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WebAuthnTransactionParent_h
#define mozilla_dom_WebAuthnTransactionParent_h

#include "mozilla/MozPromise.h"
#include "mozilla/RandomNum.h"
#include "mozilla/dom/PWebAuthnTransactionParent.h"
#include "mozilla/dom/WebAuthnPromiseHolder.h"
#include "nsIWebAuthnService.h"

/*
 * Parent process IPC implementation for WebAuthn.
 */

namespace mozilla::dom {

enum class WebAuthnOp { Create, Assert };

class WebAuthnRegisterPromiseHolder;
class WebAuthnSignPromiseHolder;
class RelatedOriginCheckHandler;  // defined in WebAuthnTransactionParent.cpp

class WebAuthnTransactionParent final : public PWebAuthnTransactionParent {
  NS_INLINE_DECL_REFCOUNTING(WebAuthnTransactionParent, override);

 public:
  WebAuthnTransactionParent();

  mozilla::ipc::IPCResult RecvRequestRegister(
      const WebAuthnMakeCredentialInfo& aTransactionInfo,
      RequestRegisterResolver&& aResolver);

  mozilla::ipc::IPCResult RecvRequestSign(
      const WebAuthnGetAssertionInfo& aTransactionInfo,
      RequestSignResolver&& aResolver);

  mozilla::ipc::IPCResult RecvRequestCancel();

  mozilla::ipc::IPCResult RecvRequestIsUVPAA(
      RequestIsUVPAAResolver&& aResolver);

  mozilla::ipc::IPCResult RecvDestroyMe();

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  friend class RelatedOriginCheckHandler;

  ~WebAuthnTransactionParent();

  void CompleteTransaction();
  void DisconnectTransaction();

  nsresult BeginRelatedOriginCheck(const nsACString& aRpId, WebAuthnOp aOp);

  void RelatedOriginApproved();
  void AbortPendingRelatedOriginCheck(nsresult aError);
  void ContinueWithRegister(const nsCString& aOrigin,
                            const WebAuthnMakeCredentialInfo& aInfo,
                            RequestRegisterResolver&& aResolver);

  void ContinueWithSign(const nsCString& aOrigin,
                        const WebAuthnGetAssertionInfo& aInfo,
                        RequestSignResolver&& aResolver);

  nsCOMPtr<nsIWebAuthnService> mWebAuthnService;
  Maybe<uint64_t> mTransactionId;
  MozPromiseRequestHolder<WebAuthnRegisterPromise> mRegisterPromiseRequest;
  MozPromiseRequestHolder<WebAuthnSignPromise> mSignPromiseRequest;
  RefPtr<RelatedOriginCheckHandler> mRelatedOriginCheckHandler;
  Maybe<WebAuthnMakeCredentialInfo> mPendingRegisterInfo;
  Maybe<RequestRegisterResolver> mPendingRegisterResolver;
  Maybe<WebAuthnGetAssertionInfo> mPendingSignInfo;
  Maybe<RequestSignResolver> mPendingSignResolver;

  // Generates a probabilistically unique ID for the new transaction. IDs are 53
  // bits, as they are used in javascript. We use a random value if possible,
  // otherwise a counter.
  static uint64_t NextId() {
    static uint64_t counter = 0;
    Maybe<uint64_t> rand = mozilla::RandomUint64();
    uint64_t id =
        rand.valueOr(++counter) & UINT64_C(0x1fffffffffffff);  // 2^53 - 1
    // The transaction ID 0 is reserved.
    return id ? id : 1;
  }
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_WebAuthnTransactionParent_h
