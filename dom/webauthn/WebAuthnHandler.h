/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WebAuthnHandler_h
#define mozilla_dom_WebAuthnHandler_h

#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/AbortSignal.h"
#include "mozilla/dom/PWebAuthnTransaction.h"
#include "mozilla/dom/PWebAuthnTransactionChild.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WebAuthnTransactionChild.h"

/*
 * Content process handler for the WebAuthn protocol. Created on calls to the
 * WebAuthentication DOM object, this is responsible for establishing IPC
 * channels for WebAuthn transactions as well as keeping track of JS Promise
 * objects representing transactions in flight.
 *
 * The WebAuthn spec (https://www.w3.org/TR/webauthn/) allows for two different
 * types of transactions: registration and signing. When either of these is
 * requested via the DOM API, the following steps are executed in the
 * WebAuthnHandler:
 *
 * - Validation of the request. Return a failed promise to js if request does
 *   not have correct parameters.
 *
 * - If request is valid, open a new IPC channel for running the transaction. If
 *   another transaction is already running in this content process, cancel it.
 *   Return a pending promise to js.
 *
 * - Send transaction information to parent process.
 *
 * - On return of successful transaction information from parent process, turn
 *   information into DOM object format required by spec, and resolve promise
 *   (by running the Finish* functions of WebAuthnHandler). On cancellation
 *   request from parent, reject promise with corresponding error code.
 *
 */

namespace mozilla::dom {

class Credential;
class PublicKeyCredential;
struct PublicKeyCredentialCreationOptions;
struct PublicKeyCredentialRequestOptions;

enum class WebAuthnTransactionType { Create, Get };

class WebAuthnTransaction {
 public:
  explicit WebAuthnTransaction(const RefPtr<Promise>& aPromise,
                               WebAuthnTransactionType aType)
      : mPromise(aPromise), mType(aType) {}

  // JS Promise representing the transaction status.
  RefPtr<Promise> mPromise;

  WebAuthnTransactionType mType;

  // These holders are used to track the transaction once it has been dispatched
  // to the parent process. Once ->Track()'d, they must either be disconnected
  // (through a call to WebAuthnHandler::CancelTransaction) or completed
  // (through a response on the IPC channel) before this WebAuthnTransaction is
  // destroyed.
  MozPromiseRequestHolder<PWebAuthnTransactionChild::RequestRegisterPromise>
      mRegisterHolder;
  MozPromiseRequestHolder<PWebAuthnTransactionChild::RequestSignPromise>
      mSignHolder;
};

class WebAuthnHandler final : public AbortFollower {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(WebAuthnHandler)

  explicit WebAuthnHandler(nsPIDOMWindowInner* aWindow) : mWindow(aWindow) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aWindow);
  }

  void MakeCredential(JSContext* aCx,
                      const PublicKeyCredentialCreationOptions& aOptions,
                      const Optional<OwningNonNull<AbortSignal>>& aSignal,
                      const RefPtr<Promise>& aPromise);

  void GetAssertion(JSContext* aCx,
                    const PublicKeyCredentialRequestOptions& aOptions,
                    const bool aConditionallyMediated,
                    const Optional<OwningNonNull<AbortSignal>>& aSignal,
                    const RefPtr<Promise>& aPromise);

  void Store(const Credential& aCredential, const RefPtr<Promise>& aPromise);

  void IsUVPAA(const RefPtr<Promise>& aPromise);

  void ActorDestroyed();

  // AbortFollower
  void RunAbortAlgorithm() override;

 private:
  virtual ~WebAuthnHandler();

  bool MaybeCreateActor();

  void FinishMakeCredential(const WebAuthnMakeCredentialResult& aResult);

  void FinishGetAssertion(const WebAuthnGetAssertionResult& aResult);

  // Send a Cancel message to the parent, reject the promise with the given
  // reason (an nsresult or JS::Handle<JS::Value>), and clear the transaction.
  template <typename T>
  void CancelTransaction(const T& aReason) {
    MOZ_ASSERT(mActor);
    MOZ_ASSERT(mTransaction.isSome());

    mTransaction.ref().mRegisterHolder.DisconnectIfExists();
    mTransaction.ref().mSignHolder.DisconnectIfExists();

    mActor->SendRequestCancel();
    RejectTransaction(aReason);
  }

  // Resolve the promise with the given credential.
  void ResolveTransaction(const RefPtr<PublicKeyCredential>& aCredential);

  // Reject the promise with the given reason (an nsresult or JS::Value), and
  // clear the transaction.
  template <typename T>
  void RejectTransaction(const T& aReason);

  // The parent window.
  nsCOMPtr<nsPIDOMWindowInner> mWindow;

  // IPC Channel to the parent process.
  RefPtr<WebAuthnTransactionChild> mActor;

  // The current transaction, if any.
  Maybe<WebAuthnTransaction> mTransaction;
};

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    WebAuthnTransaction& aTransaction, const char* aName, uint32_t aFlags = 0) {
  ImplCycleCollectionTraverse(aCallback, aTransaction.mPromise, aName, aFlags);
}

inline void ImplCycleCollectionUnlink(WebAuthnTransaction& aTransaction) {
  ImplCycleCollectionUnlink(aTransaction.mPromise);
}

}  // namespace mozilla::dom

#endif  // mozilla_dom_WebAuthnHandler_h
