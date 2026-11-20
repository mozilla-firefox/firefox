/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Http3TestStubs.h"

#include "nsHttpHandler.h"
#include "nsIIOService.h"
#include "nsIProtocolHandler.h"
#include "nsNetUtil.h"

namespace mozilla::net {

NS_IMPL_ISUPPORTS(DummyHttpTransaction, nsISupportsWeakReference)

void InitHttpHandler() {
  if (gHttpHandler) {
    return;
  }

  nsresult rv;
  nsCOMPtr<nsIIOService> ios = do_GetIOService(&rv);
  if (NS_FAILED(rv)) {
    return;
  }

  nsCOMPtr<nsIProtocolHandler> handler;
  rv = ios->GetProtocolHandler("http", getter_AddRefs(handler));
  if (NS_FAILED(rv)) {
    return;
  }
}

}  // namespace mozilla::net
