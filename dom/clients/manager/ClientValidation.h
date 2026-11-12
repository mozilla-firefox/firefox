/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _mozilla_dom_ClientValidation_h
#define _mozilla_dom_ClientValidation_h

#include "mozilla/Maybe.h"
#include "mozilla/Tainting.h"
#include "nsStringFwd.h"

namespace mozilla {

namespace ipc {
class PrincipalInfo;
}  // namespace ipc

namespace dom {

class ClientOpConstructorArgs;
class IPCClientInfo;
class LoadedOriginSet;

Maybe<IPCClientInfo&> ValidateClientInfo(Tainted<IPCClientInfo>& aClientInfo,
                                         LoadedOriginSet* aLoadedOrigins);

bool ClientIsValidPrincipalInfo(
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
    LoadedOriginSet* aLoadedOrigins);

bool IsValidClientOpConstructorArgs(const ClientOpConstructorArgs& aArgs,
                                    LoadedOriginSet* aLoadedOrigins);

bool ClientIsValidCreationURL(const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
                              const nsACString& aURL);

}  // namespace dom
}  // namespace mozilla

#endif  // _mozilla_dom_ClientValidation_h
