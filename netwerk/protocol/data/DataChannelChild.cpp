/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=4 sw=2 sts=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DataChannelChild.h"

#include "mozilla/Unused.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/net/NeckoChild.h"

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS_INHERITED(DataChannelChild, nsDataChannel, nsIChildChannel)

DataChannelChild::DataChannelChild(nsIURI* aURI)
    : nsDataChannel(aURI), mIPCOpen(false) {}

NS_IMETHODIMP
DataChannelChild::ConnectParent(uint32_t aId) {
  mozilla::dom::ContentChild* cc =
      static_cast<mozilla::dom::ContentChild*>(gNeckoChild->Manager());
  if (cc->IsShuttingDown()) {
    return NS_ERROR_FAILURE;
  }

  if (!gNeckoChild->SendPDataChannelConstructor(this, aId)) {
    return NS_ERROR_FAILURE;
  }

  // IPC now has a ref to us.
  mIPCOpen = true;
  return NS_OK;
}

NS_IMETHODIMP
DataChannelChild::CompleteRedirectSetup(nsIStreamListener* aListener) {
  nsresult rv;
  rv = AsyncOpen(aListener);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (mIPCOpen) {
    Unused << Send__delete__(this);
  }
  return NS_OK;
}

void DataChannelChild::ActorDestroy(ActorDestroyReason why) {
  MOZ_ASSERT(mIPCOpen);
  mIPCOpen = false;
}

}  // namespace net
}  // namespace mozilla
