/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WebAuthnIPCUtils_h
#define mozilla_dom_WebAuthnIPCUtils_h

#include "mozilla/dom/BindingIPCUtils.h"
#include "mozilla/dom/WebAuthenticationBinding.h"

namespace IPC {
template <>
struct ParamTraits<mozilla::dom::CredentialProtectionPolicy>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::CredentialProtectionPolicy> {};
}  // namespace IPC

#endif  // mozilla_dom_WebAuthnIPCUtils_h
