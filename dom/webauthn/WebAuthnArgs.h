/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WebAuthnArgs_H_
#define mozilla_dom_WebAuthnArgs_H_

#include "mozilla/dom/WebAuthnTransactionChild.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsIWebAuthnArgs.h"

namespace mozilla::dom {

class WebAuthnRegisterArgs final : public nsIWebAuthnRegisterArgs {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWEBAUTHNREGISTERARGS

  explicit WebAuthnRegisterArgs(const nsCString& aOrigin,
                                const nsCString& aClientDataJSON,
                                const bool aPrivateBrowsing,
                                const WebAuthnMakeCredentialInfo& aInfo)
      : mOrigin(aOrigin),
        mClientDataJSON(aClientDataJSON),
        mPrivateBrowsing(aPrivateBrowsing),
        mInfo(aInfo),
        mEnforceCredentialProtectionPolicy(false),
        mCredProps(false),
        mHmacCreateSecret(false),
        mLargeBlobSupportRequired(Nothing()),
        mMinPinLength(false),
        mPrf(false) {
    for (const WebAuthnExtension& ext : mInfo.Extensions()) {
      switch (ext.type()) {
        case WebAuthnExtension::TWebAuthnExtensionCredProtect:
          mCredentialProtectionPolicy.emplace(
              ext.get_WebAuthnExtensionCredProtect().policy());
          mEnforceCredentialProtectionPolicy =
              ext.get_WebAuthnExtensionCredProtect().required();
          break;
        case WebAuthnExtension::TWebAuthnExtensionCredProps:
          mCredProps = ext.get_WebAuthnExtensionCredProps().credProps();
          break;
        case WebAuthnExtension::TWebAuthnExtensionHmacSecret:
          mHmacCreateSecret =
              ext.get_WebAuthnExtensionHmacSecret().hmacCreateSecret();
          break;
        case WebAuthnExtension::TWebAuthnExtensionLargeBlob:
          mLargeBlobSupportRequired =
              ext.get_WebAuthnExtensionLargeBlob().flag();
          break;
        case WebAuthnExtension::TWebAuthnExtensionMinPinLength:
          mMinPinLength =
              ext.get_WebAuthnExtensionMinPinLength().minPinLength();
          break;
        case WebAuthnExtension::TWebAuthnExtensionPrf:
          mPrf = true;
          break;
        case WebAuthnExtension::T__None:
          break;
      }
    }
  }

 private:
  ~WebAuthnRegisterArgs() = default;

  const nsCString mOrigin;
  const nsCString mClientDataJSON;
  const bool mPrivateBrowsing;
  const WebAuthnMakeCredentialInfo mInfo;

  Maybe<CredentialProtectionPolicy> mCredentialProtectionPolicy;
  bool mEnforceCredentialProtectionPolicy;

  // Flags to indicate whether an extension is being requested.
  bool mCredProps;
  bool mHmacCreateSecret;
  Maybe<bool> mLargeBlobSupportRequired;
  bool mMinPinLength;
  bool mPrf;
};

class WebAuthnSignArgs final : public nsIWebAuthnSignArgs {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWEBAUTHNSIGNARGS

  explicit WebAuthnSignArgs(const nsCString& aOrigin,
                            const nsCString& aClientDataJSON,
                            const bool aPrivateBrowsing,
                            const WebAuthnGetAssertionInfo& aInfo)
      : mOrigin(aOrigin),
        mClientDataJSON(aClientDataJSON),
        mPrivateBrowsing(aPrivateBrowsing),
        mInfo(aInfo),
        mPrf(false) {
    for (const WebAuthnExtension& ext : mInfo.Extensions()) {
      switch (ext.type()) {
        case WebAuthnExtension::TWebAuthnExtensionCredProtect:
          break;
        case WebAuthnExtension::TWebAuthnExtensionCredProps:
          break;
        case WebAuthnExtension::TWebAuthnExtensionHmacSecret:
          break;
        case WebAuthnExtension::TWebAuthnExtensionMinPinLength:
          break;
        case WebAuthnExtension::TWebAuthnExtensionLargeBlob:
          if (ext.get_WebAuthnExtensionLargeBlob().flag().isSome()) {
            bool read = ext.get_WebAuthnExtensionLargeBlob().flag().ref();
            mLargeBlobRead.emplace(read);
            if (!read) {
              mLargeBlobWrite.AppendElements(
                  ext.get_WebAuthnExtensionLargeBlob().write());
            }
          }
          break;
        case WebAuthnExtension::TWebAuthnExtensionPrf:
          mPrf = ext.get_WebAuthnExtensionPrf().eval().isSome() ||
                 ext.get_WebAuthnExtensionPrf().evalByCredentialMaybe();
          break;
        case WebAuthnExtension::T__None:
          break;
      }
    }
  }

 private:
  ~WebAuthnSignArgs() = default;

  const nsCString mOrigin;
  const nsCString mClientDataJSON;
  const bool mPrivateBrowsing;
  const WebAuthnGetAssertionInfo mInfo;
  Maybe<bool> mLargeBlobRead;
  nsTArray<uint8_t> mLargeBlobWrite;
  bool mPrf;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_WebAuthnArgs_H_
