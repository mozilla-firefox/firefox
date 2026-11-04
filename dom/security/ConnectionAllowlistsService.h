/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ConnectionAllowlistsService_h_
#define mozilla_dom_ConnectionAllowlistsService_h_

#include "mozilla/Maybe.h"
#include "nsIContentPolicy.h"

class nsILoadInfo;
class nsIURI;

namespace mozilla::dom {

// Content policy enforcing connection allowlists.
// https://w3c.github.io/connection-allowlists/
class ConnectionAllowlistsService final : public nsIContentPolicy {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTPOLICY

  ConnectionAllowlistsService() = default;

  // Static helper to check the Connection Allowlists when doing a channel
  // redirect, called from nsContentPolicy::AsyncOnChannelRedirect.
  static nsresult ConsultForRedirect(nsIURI* aOriginalURI, nsIURI* aNewURI,
                                     nsILoadInfo* aLoadInfo, uint32_t aFlags,
                                     Maybe<nsresult>& aCancelCode);

 private:
  ~ConnectionAllowlistsService() = default;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_ConnectionAllowlistsService_h_ */
