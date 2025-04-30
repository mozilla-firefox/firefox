/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_identitycredentialserializationhelpers_h__
#define mozilla_dom_identitycredentialserializationhelpers_h__

#include "mozilla/dom/BindingIPCUtils.h"
#include "mozilla/dom/IdentityCredential.h"
#include "mozilla/dom/IdentityCredentialBinding.h"
#include "mozilla/dom/CredentialManagementBinding.h"
#include "mozilla/dom/LoginStatusBinding.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::dom::IdentityProviderConfig> {
  typedef mozilla::dom::IdentityProviderConfig paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mConfigURL);
    WriteParam(aWriter, aParam.mClientId);
    WriteParam(aWriter, aParam.mNonce);
    WriteParam(aWriter, aParam.mLoginHint);
    WriteParam(aWriter, aParam.mDomainHint);
    WriteParam(aWriter, aParam.mOrigin);
    WriteParam(aWriter, aParam.mLoginURL);
    WriteParam(aWriter, aParam.mLoginTarget);
    WriteParam(aWriter, aParam.mEffectiveQueryURL);
    WriteParam(aWriter, aParam.mEffectiveType);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mConfigURL) &&
           ReadParam(aReader, &aResult->mClientId) &&
           ReadParam(aReader, &aResult->mNonce) &&
           ReadParam(aReader, &aResult->mLoginHint) &&
           ReadParam(aReader, &aResult->mDomainHint) &&
           ReadParam(aReader, &aResult->mOrigin) &&
           ReadParam(aReader, &aResult->mLoginURL) &&
           ReadParam(aReader, &aResult->mLoginTarget) &&
           ReadParam(aReader, &aResult->mEffectiveQueryURL) &&
           ReadParam(aReader, &aResult->mEffectiveType);
  }
};

template <>
struct ParamTraits<mozilla::dom::IdentityCredentialDisconnectOptions> {
  typedef mozilla::dom::IdentityCredentialDisconnectOptions paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mConfigURL);
    WriteParam(aWriter, aParam.mClientId);
    WriteParam(aWriter, aParam.mAccountHint);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mConfigURL) &&
           ReadParam(aReader, &aResult->mClientId) &&
           ReadParam(aReader, &aResult->mAccountHint);
  }
};

template <>
struct ParamTraits<mozilla::dom::IdentityLoginTargetType>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::IdentityLoginTargetType> {};

template <>
struct ParamTraits<mozilla::dom::CredentialMediationRequirement>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::CredentialMediationRequirement> {};

template <>
struct ParamTraits<mozilla::dom::IdentityCredentialRequestOptions> {
  typedef mozilla::dom::IdentityCredentialRequestOptions paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mProviders);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mProviders);
  }
};

template <>
struct ParamTraits<mozilla::dom::LoginStatus>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::LoginStatus> {};

}  // namespace IPC

#endif  // mozilla_dom_identitycredentialserializationhelpers_h__
