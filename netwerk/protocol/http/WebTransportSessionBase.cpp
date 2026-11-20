/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebTransportSessionBase.h"

#include "nsIWebTransport.h"

namespace mozilla::net {

NS_IMPL_ISUPPORTS(WebTransportSessionStatsWrapper, nsIWebTransportSessionStats)

NS_IMETHODIMP WebTransportSessionStatsWrapper::GetRawStats(
    mozilla::dom::WebTransportStatsData** aStats) {
  *aStats = &mStats;
  return NS_OK;
}

void WebTransportSessionBase::SetWebTransportSessionEventListener(
    WebTransportSessionEventListener* listener) {
  MutexAutoLock lock(mListenerLock);
  mListener = listener;
}

already_AddRefed<WebTransportSessionEventListener>
WebTransportSessionBase::GetListener() {
  MutexAutoLock lock(mListenerLock);
  return do_AddRef(mListener);
}

already_AddRefed<WebTransportSessionEventListener>
WebTransportSessionBase::TakeListener() {
  MutexAutoLock lock(mListenerLock);
  return mListener.forget();
}

}  // namespace mozilla::net
