/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCSPService_h_
#define nsCSPService_h_

#include "mozilla/Maybe.h"
#include "nsIContentPolicy.h"
#include "nsXPCOM.h"

#define CSPSERVICE_CONTRACTID "@mozilla.org/cspservice;1"
#define CSPSERVICE_CID \
  {0x8d2f40b2, 0x4875, 0x4c95, {0x97, 0xd9, 0x3f, 0x7d, 0xca, 0x2c, 0xb4, 0x60}}
class CSPService : public nsIContentPolicy {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTPOLICY

  CSPService();

  // helper function to avoid creating a new instance of the
  // cspservice everytime we call content policies.
  static nsresult ConsultCSP(nsIURI* aContentLocation, nsILoadInfo* aLoadInfo,
                             int16_t* aDecision);

  // Static helper to check CSP when doing a channel redirect, called from
  // nsContentPolicy::AsyncOnChannelRedirect.
  // Returns the results to returns from
  // AsyncOnChannelRedirect/nsIAsyncVerifyRedirectCallback. Optionally returns
  // an nsresult to Cancel the old channel with.
  static nsresult ConsultCSPForRedirect(nsIURI* aOriginalURI, nsIURI* aNewURI,
                                        nsILoadInfo* aLoadInfo, uint32_t aFlags,
                                        mozilla::Maybe<nsresult>& aCancelCode);

 protected:
  virtual ~CSPService();
};
#endif /* nsCSPService_h_ */
