/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_IdentityCredential_h
#define mozilla_dom_IdentityCredential_h

#include "mozilla/IdentityCredentialStorageService.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Credential.h"
#include "mozilla/dom/PWebIdentity.h"

namespace mozilla::dom {

// This is the primary starting point for FedCM in the platform.
// This class is the implementation of the IdentityCredential object
// that is the value returned from the navigator.credentials.get call
// with an "identity" argument. It also includes static functions that
// perform operations that are used in constructing the credential.
class IdentityCredential final : public Credential {
  friend class mozilla::IdentityCredentialStorageService;
  friend class WindowGlobalChild;

 protected:
  ~IdentityCredential() override;

 public:
  explicit IdentityCredential(nsPIDOMWindowInner* aParent,
                              const IPCIdentityCredential& aOther);

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  // This builds a value from an IPC-friendly version. This type is returned
  // to the caller of navigator.credentials.get, however we get an IPC friendly
  // version back from the main process to the content process.
  // This is a deep copy of the token, ID, and type.
  void CopyValuesFrom(const IPCIdentityCredential& aOther);

  // This is the inverse of CopyValuesFrom. Included for completeness.
  IPCIdentityCredential MakeIPCIdentityCredential() const;

  static already_AddRefed<Promise> Disconnect(
      const GlobalObject& aGlobal,
      const IdentityCredentialDisconnectOptions& aOptions, ErrorResult& aRv);
  // Getter and setter for the token member of this class
  void GetToken(nsACString& aToken) const;
  void SetToken(const nsACString& aToken);
  void GetConfigURL(nsACString& aConfigURL) const;
  void SetConfigURL(const nsACString& aConfigURL);
  bool IsAutoSelected() const;

 private:
  nsCString mToken;
  nsCString mConfigURL;
  bool mIsAutoSelected;
};
}  // namespace mozilla::dom

#endif  // mozilla_dom_IdentityCredential_h
